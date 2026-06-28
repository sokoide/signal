/*
 * 09_fork_exec.c
 *
 * Topic: Signal disposition inheritance across fork() and exec().
 *
 * POSIX defines exactly what survives when a process forks or execs another
 * program.  This program demonstrates each rule with concrete output:
 *
 *   fork():
 *     - Signal handlers are inherited (the child starts with the same
 *       handler function pointers as the parent).
 *     - The signal mask is inherited.
 *     - The signal mask is inherited.
 *     - Pending signals are *NOT* inherited; the child's pending set is
 *       cleared at the moment of fork().
 *     - The alternate signal stack (sigaltstack()) is inherited on many
 *       systems such as Linux, but is implementation-defined; on some
 *       platforms (e.g. macOS) it is cleared for the child.
 *
 *   exec():
 *     - Signal handlers for caught signals are reset to SIG_DFL.
 *     - Signal handlers that were set to SIG_IGN stay ignored.
 *     - The signal mask is preserved.
 *     - The alternate signal stack is cleared (disabled).
 *
 * Usage:
 *   cc -std=c11 -Wall -Wextra -O2 -g 09_fork_exec.c -o 09_fork_exec
 *   ./09_fork_exec
 *
 * The program re-executes itself with the "--after-exec" argument so that
 * the post-exec state can be inspected inside the same binary.
 */

#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

/* Holds the malloc'd alternate signal stack so we can free it on clean exit. */
static void* alt_stack_mem = NULL;

/* A tiny handler installed for SIGUSR1 in the parent. */
static void usr1_handler(int sig) {
    (void)sig; /* unused; silence -Wextra */
}

/*
 * Print a single-line description of a signal disposition.
 * We query with sigaction() without changing the disposition by passing
 * NULL as the second argument (the new action).
 */
static void print_disposition(int sig, const char* name) {
    struct sigaction old;
    if (sigaction(sig, NULL, &old) == -1) {
        perror("sigaction");
        return;
    }

    printf("    %s: ", name);
    if (old.sa_handler == SIG_DFL) {
        printf("SIG_DFL (default action)\n");
    } else if (old.sa_handler == SIG_IGN) {
        printf("SIG_IGN (ignored)\n");
    } else {
        /* Function pointers print as an address. */
        printf("caught by %p\n", (void*)old.sa_handler);
    }
}

/* Print whether a signal is currently blocked in the caller's mask. */
static void print_blocked(int sig, const char* name) {
    sigset_t mask;
    if (sigprocmask(SIG_BLOCK, NULL, &mask) == -1) {
        perror("sigprocmask");
        return;
    }

    printf("    %s is %s\n", name,
           sigismember(&mask, sig) ? "BLOCKED" : "NOT blocked");
}

/* Print the current alternate signal stack state. */
static void print_alt_stack(void) {
    stack_t ss;
    if (sigaltstack(NULL, &ss) == -1) {
        perror("sigaltstack");
        return;
    }

    if (ss.ss_flags & SS_DISABLE) {
        printf("    alternate stack: DISABLED\n");
    } else {
        printf("    alternate stack: ENABLED at %p, size %zu\n", ss.ss_sp,
               ss.ss_size);
    }
}

/* Print the current pending signal set in human-readable form. */
static void print_pending(void) {
    sigset_t pending;
    if (sigpending(&pending) == -1) {
        perror("sigpending");
        return;
    }

    int has_sigusr1 = sigismember(&pending, SIGUSR1);
    int has_sigusr2 = sigismember(&pending, SIGUSR2);

    if (!has_sigusr1 && !has_sigusr2) {
        printf("    pending signals: (none)\n");
    } else {
        printf("    pending signals: %s%s%s\n", has_sigusr1 ? "SIGUSR1" : "",
               (has_sigusr1 && has_sigusr2) ? ", " : "",
               has_sigusr2 ? "SIGUSR2" : "");
    }
}

/*
 * Display the complete signal environment of the current process.
 * This is called from three different execution contexts in the demo.
 */
static void print_status(const char* label) {
    printf("\n== %s ==\n", label);
    print_disposition(SIGUSR1, "SIGUSR1");
    print_disposition(SIGPIPE, "SIGPIPE");
    print_blocked(SIGUSR2, "SIGUSR2");
    print_pending();
    print_alt_stack();
}

/* Set a signal handler using sigaction(). */
static void set_handler(int sig, void (*handler)(int)) {
    struct sigaction sa;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;
    sa.sa_handler = handler;
    if (sigaction(sig, &sa, NULL) == -1) {
        perror("sigaction");
        exit(EXIT_FAILURE);
    }
}

/* Set a signal disposition to SIG_IGN using sigaction(). */
static void set_ignore(int sig) {
    struct sigaction sa;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;
    sa.sa_handler = SIG_IGN;
    if (sigaction(sig, &sa, NULL) == -1) {
        perror("sigaction");
        exit(EXIT_FAILURE);
    }
}

/* Block or unblock a single signal. */
static void set_block(int sig, int block) {
    sigset_t set;
    sigemptyset(&set);
    sigaddset(&set, sig);
    if (sigprocmask(block ? SIG_BLOCK : SIG_UNBLOCK, &set, NULL) == -1) {
        perror("sigprocmask");
        exit(EXIT_FAILURE);
    }
}

/* Install an alternate signal stack. */
static void setup_alt_stack(void) {
    stack_t ss;
    /* SIGSTKSZ may be non-constant on glibc >= 2.34.
     * MINSIGSTKSZ is always a compile-time constant and is the
     * minimum size POSIX guarantees for a signal handler. */
    ss.ss_size = MINSIGSTKSZ;
    alt_stack_mem = malloc(ss.ss_size);
    ss.ss_sp = alt_stack_mem;
    if (ss.ss_sp == NULL) {
        perror("malloc");
        exit(EXIT_FAILURE);
    }
    ss.ss_flags = 0;
    if (sigaltstack(&ss, NULL) == -1) {
        perror("sigaltstack");
        exit(EXIT_FAILURE);
    }
}

/*
 * This branch runs after the child calls exec() with the argument
 * "--after-exec".  It shows the signal state that survives an exec.
 */
static int after_exec_branch(void) {
    print_status("After exec() in child");
    fflush(stdout);
    return EXIT_SUCCESS;
}

/*
 * The main demonstration branch.  It prepares the parent's signal
 * environment, forks, and lets the child inspect and then exec itself.
 */
static int main_branch(char* argv0) {
    /*
     * 1. Parent setup:
     *    - Catch SIGUSR1.
     *    - Ignore SIGPIPE.
     *    - Block SIGUSR2.
     *    - Set up an alternate signal stack.
     */
    set_handler(SIGUSR1, usr1_handler);
    set_ignore(SIGPIPE);
    set_block(SIGUSR2, 1);
    setup_alt_stack();

    /*
     * Demonstrate pending-signal inheritance rules.
     * We raise SIGUSR2 while it is blocked, so it becomes pending in the
     * parent only.  fork() will *not* copy this pending signal to the child.
     */
    if (raise(SIGUSR2) != 0) {
        perror("raise");
        exit(EXIT_FAILURE);
    }

    printf("Parent state before fork():\n");
    print_status("Parent before fork()");
    /*
     * Flush stdout before forking so the child does not inherit a buffered
     * copy of the lines above and print them again.
     */
    fflush(stdout);

    /*
     * 2. fork() creates a child that is an exact copy of the parent's
     *    memory and signal state, except that pending signals are cleared.
     */
    pid_t pid = fork();
    if (pid == -1) {
        perror("fork");
        exit(EXIT_FAILURE);
    }

    if (pid == 0) {
        /*
         * Child process.
         *
         * Because of copy-on-write, the address of usr1_handler is the
         * same as in the parent and the signal mask is copied.
         * SIGUSR2, raised before fork(), should NOT be pending here
         * because pending signals are cleared for the child.
         *
         * The alternate stack may or may not be inherited depending on the
         * operating system (inherited on Linux, cleared on macOS).
         */
        print_status("After fork() in child");
        fflush(stdout);

        /*
         * 3. exec() the same binary with "--after-exec".
         *
         * After exec():
         *   - Caught handlers (SIGUSR1) revert to SIG_DFL.
         *   - Ignored handlers (SIGPIPE) remain SIG_IGN.
         *   - The signal mask is preserved.
         *   - The alternate stack is cleared.
         */
        execl(argv0, argv0, "--after-exec", (char*)NULL);

        /* execl() only returns on failure. */
        perror("execl");
        _exit(EXIT_FAILURE);
    }

    /* Parent: wait for the child to finish the demo. */
    int status;
    if (waitpid(pid, &status, 0) == -1) {
        perror("waitpid");
        exit(EXIT_FAILURE);
    }

    if (WIFEXITED(status) && WEXITSTATUS(status) == EXIT_SUCCESS) {
        printf("\nChild exited successfully.\n");
    } else {
        printf("\nChild did not exit successfully.\n");
    }

    printf("\n");
    printf("Summary of POSIX signal inheritance rules:\n");
    printf("  fork():  signal handlers are inherited.\n");
    printf("  fork():  signal mask is inherited.\n");
    printf("  fork():  pending signals are CLEARED for the child.\n");
    printf(
        "  fork():  alternate stack inheritance is implementation-defined\n");
    printf("           (inherited on Linux, often cleared on macOS).\n");
    printf("  exec():  caught handlers reset to SIG_DFL.\n");
    printf("  exec():  ignored handlers (SIG_IGN) remain ignored.\n");
    printf("  exec():  signal mask is preserved.\n");
    printf("  exec():  alternate signal stack is cleared.\n");

    free(alt_stack_mem);
    return EXIT_SUCCESS;
}

int main(int argc, char* argv[]) {
    /*
     * argv[0] is used to re-exec ourselves.  If it is missing, we cannot
     * continue the demonstration.
     */
    if (argc > 1 && strcmp(argv[1], "--after-exec") == 0) {
        return after_exec_branch();
    }

    return main_branch(argv[0]);
}
