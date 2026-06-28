/*
 * 07_selfpipe.c — The Self-Pipe Trick
 *
 * Topic: converting asynchronous POSIX signals into file-descriptor events
 * so they can be handled inside a single select()/poll()/epoll() event loop.
 *
 * The problem
 * -----------
 * Event-driven programs wait for activity on file descriptors using
 * select(), poll(), epoll(), kqueue(), etc.  These APIs can only watch FDs.
 * Signals are *not* file descriptors, so a signal arriving while the main
 * loop is blocked in select() does not by itself make select() return.
 * The program therefore needs a bridge between "signal arrived" and
 * "FD became readable".
 *
 * The solution (self-pipe)
 * ------------------------
 * Create a pipe.  In the signal handler, perform the smallest possible
 * async-signal-safe action: write one byte to the pipe.  The byte we write
 * is the signal number, so the main loop can tell *which* signal arrived.
 * In the main loop, include the read end of the pipe in the FD set passed
 * to select().  When select() reports that the pipe is readable, drain it
 * and process the signal in normal program context — where printf(), malloc(),
 * and arbitrary logic are safe.
 *
 * Why this is safe
 * ----------------
 * 1. write(2) is async-signal-safe, so calling it from a signal handler
 *    does not risk deadlock or corrupting stdio/heap locks.
 * 2. The write end is made non-blocking (O_NONBLOCK).  Even if many signals
 *    arrive in a burst, write() returns EAGAIN instead of blocking forever
 *    inside the handler.
 * 3. The read end is read by the main loop, not by the handler, so complex
 *    processing happens in normal context.
 *
 * Linux alternative
 * -----------------
 * Linux provides signalfd(2), which creates a file descriptor that becomes
 * readable when a signal arrives.  It is cleaner but Linux-specific.
 * The self-pipe trick is fully POSIX and works on macOS, *BSD, etc.
 *
 * Build:  cc -std=c11 -Wall -Wextra -O2 -g examples/07_selfpipe.c -o
 * 07_selfpipe Run:    ./07_selfpipe Test:   Press Ctrl-C (SIGINT) or send
 * SIGTERM from another terminal.
 */

#define _POSIX_C_SOURCE 200809L
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/select.h>
#include <unistd.h>

/* The pipe file descriptors.  Index 0 is the read end, index 1 the write end.
 * These are global so the signal handler can write to the pipe. */
static int selfpipe[2] = {-1, -1};

/* Count of signals dropped because the self-pipe buffer was full.
 * This is volatile sig_atomic_t because it is written by the handler. */
static volatile sig_atomic_t selfpipe_overflow_count = 0;

/*
 * Signal handler.
 *
 * This runs in signal context, so we must use only async-signal-safe
 * functions.  We only call write().  We write a single byte: the signal
 * number that caused the handler to run.  The main loop will read this
 * byte and handle the signal in normal context.
 */
static void selfpipe_handler(int sig) {
    /* The signal number fits in one byte (signals are 1-31 for standard
     * signals, and up to 64 on Linux for real-time signals). */
    unsigned char c = (unsigned char)sig;

    /* write() is async-signal-safe.  Making the write end non-blocking
     * prevents us from blocking here. If signals arrive faster than the
     * main loop drains the pipe, the pipe buffer (typically 16-64 KiB)
     * can fill up and write() returns EAGAIN. We count those drops so
     * the demo can report the limitation.
     *
     * This is an inherent limitation of the self-pipe pattern.
     * The Linux-specific signalfd(2) avoids this issue. */
    if (write(selfpipe[1], &c, 1) == -1 && errno == EAGAIN) {
        selfpipe_overflow_count++;
    }
}

/*
 * Set a file descriptor to non-blocking mode.
 * This is important for the write end of the self-pipe: if a burst of
 * signals arrives while the pipe buffer is full, write() will return -1
 * with EAGAIN instead of blocking inside the signal handler.
 */
static int set_nonblocking(int fd) {
    int flags = fcntl(fd, F_GETFL, 0);
    if (flags == -1) {
        return -1;
    }
    if (fcntl(fd, F_SETFL, flags | O_NONBLOCK) == -1) {
        return -1;
    }
    return 0;
}

/*
 * Register a signal that should be forwarded through the self-pipe.
 */
static int register_selfpipe_signal(int sig) {
    struct sigaction sa;

    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = selfpipe_handler;

    /* Block the same signal while the handler runs.  This is the default
     * behavior even without SA_NODEFER, but being explicit is educational. */
    if (sigemptyset(&sa.sa_mask) == -1) {
        return -1;
    }
    if (sigaddset(&sa.sa_mask, sig) == -1) {
        return -1;
    }

    /* We do NOT use SA_RESTART here.  If a signal arrives while the main
     * thread is blocked in select(), we *want* select() to return with
     * EINTR so that the FD set can be re-evaluated.  In practice, select()
     * will return because the self-pipe became readable, not because of
     * EINTR, but restarting would be counter-productive. */
    sa.sa_flags = 0;

    if (sigaction(sig, &sa, NULL) == -1) {
        return -1;
    }
    return 0;
}

int main(void) {
    /*
     * Step 1: create the self-pipe.
     * selfpipe[0] = read end (monitored by select)
     * selfpipe[1] = write end (written by signal handler)
     */
    if (pipe(selfpipe) == -1) {
        perror("pipe");
        return EXIT_FAILURE;
    }

    /*
     * Step 2: make the write end non-blocking.
     * Without this, a signal storm could block the handler forever.
     */
    if (set_nonblocking(selfpipe[1]) == -1) {
        perror("fcntl O_NONBLOCK");
        close(selfpipe[0]);
        close(selfpipe[1]);
        return EXIT_FAILURE;
    }

    /*
     * Step 3: install signal handlers that write to the self-pipe.
     * We demonstrate SIGINT (Ctrl-C) and SIGTERM (kill -TERM).
     */
    if (register_selfpipe_signal(SIGINT) == -1) {
        perror("sigaction SIGINT");
        close(selfpipe[0]);
        close(selfpipe[1]);
        return EXIT_FAILURE;
    }
    if (register_selfpipe_signal(SIGTERM) == -1) {
        perror("sigaction SIGTERM");
        close(selfpipe[0]);
        close(selfpipe[1]);
        return EXIT_FAILURE;
    }

    printf("Self-pipe demo running.  PID=%d\n", (int)getpid());
    printf("Press Enter, or send SIGINT (Ctrl-C) / SIGTERM.\n");
    printf("Send signals from another terminal with:\n");
    printf("  kill -INT %d\n", (int)getpid());
    printf("  kill -TERM %d\n", (int)getpid());
    fflush(stdout);

    /*
     * Step 4: main event loop using select().
     * We monitor two FDs:
     *   - selfpipe[0] : signals converted to FD events
     *   - STDIN_FILENO: ordinary terminal input
     */
    for (;;) {
        fd_set rfds;
        int maxfd;
        int ret;

        FD_ZERO(&rfds);
        FD_SET(selfpipe[0], &rfds);
        FD_SET(STDIN_FILENO, &rfds);

        maxfd = selfpipe[0] > STDIN_FILENO ? selfpipe[0] : STDIN_FILENO;

        /* select() blocks until one of the watched FDs becomes readable. */
        ret = select(maxfd + 1, &rfds, NULL, NULL, NULL);
        if (ret == -1) {
            /* EINTR is possible if a signal arrives just before select()
             * blocks and the kernel restarts it.  With our self-pipe,
             * the pipe will be readable next iteration, so we simply loop. */
            if (errno == EINTR) {
                continue;
            }
            perror("select");
            break;
        }

        /*
         * Case A: the self-pipe is readable -> a signal arrived.
         * We drain the pipe in a loop to handle all bytes that may have
         * accumulated during a burst.
         */
        if (FD_ISSET(selfpipe[0], &rfds)) {
            unsigned char buf[16];
            ssize_t n;

            /*
             * Read in a loop until the pipe is empty.  This handles the
             * edge case where many signals arrive very quickly.
             * EAGAIN means the pipe is drained; that is expected and safe.
             */
            while ((n = read(selfpipe[0], buf, sizeof(buf))) > 0) {
                for (ssize_t i = 0; i < n; ++i) {
                    int sig = buf[i];
                    printf("[event loop] Signal converted to FD event: sig=%d",
                           sig);
                    if (sig == SIGINT) {
                        printf(" (SIGINT)\n");
                    } else if (sig == SIGTERM) {
                        printf(" (SIGTERM)\n");
                    } else {
                        printf("\n");
                    }
                    fflush(stdout);

                    /* For this demo, either signal triggers clean shutdown. */
                    if (sig == SIGINT || sig == SIGTERM) {
                        printf("[event loop] Shutting down cleanly.\n");
                        if (selfpipe_overflow_count > 0) {
                            printf(
                                "[event loop] Note: %d signal(s) were "
                                "dropped due to a full self-pipe buffer.\n",
                                (int)selfpipe_overflow_count);
                        }
                        close(selfpipe[0]);
                        close(selfpipe[1]);
                        return EXIT_SUCCESS;
                    }
                }
            }

            if (n == -1 && errno != EAGAIN && errno != EINTR) {
                perror("read selfpipe");
                break;
            }
        }

        /*
         * Case B: stdin is readable -> ordinary input.
         */
        if (FD_ISSET(STDIN_FILENO, &rfds)) {
            char line[256];
            if (fgets(line, sizeof(line), stdin) == NULL) {
                printf("[event loop] EOF on stdin, exiting.\n");
                break;
            }
            printf("[event loop] Read line: %s", line);
            fflush(stdout);
        }
    }

    /* Cleanup. */
    close(selfpipe[0]);
    close(selfpipe[1]);
    return EXIT_SUCCESS;
}
