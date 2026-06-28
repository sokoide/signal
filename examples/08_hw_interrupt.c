/*
 * 08_hw_interrupt.c — Structural equivalence of HW interrupts and POSIX signals
 *
 * This is the central example of the repository.
 *
 * It demonstrates that a POSIX signal handler is, structurally, the exact
 * user-space analogue of a CPU hardware-interrupt service routine (ISR).
 *
 * Hardware interrupt flow (x86_64)        POSIX signal flow
 * ------------------------------          -------------------
 * 1. Device raises IRQ (timer PIT)        1. kill() / setitimer() generates
 * signal
 * 2. CPU pushes SS:RSP/RFLAGS/CS:RIP      2. Kernel saves full register state
 *    onto the kernel stack                    into a ucontext_t (signal frame)
 * 3. CPU uses IDT[vector] to find ISR     3. Kernel uses sigaction table
 * 4. ISR executes                         4. Signal handler executes
 * 5. iret restores registers, resumes     5. sigreturn() restores ucontext_t,
 *    main program                              resumes main program
 *
 * In this program, SIGVTALRM driven by setitimer(ITIMER_VIRTUAL) acts as a
 * "timer interrupt" for the process.  The handler receives the third argument
 * ucontext_t*, which is the trap frame: the complete saved CPU context of the
 * interrupted user code.  We print the program counter (RIP), stack pointer
 * (RSP), and frame pointer (RBP) to show that the kernel really did freeze the
 * main program in the middle of its execution.
 *
 * Why only write() in the handler?
 * --------------------------------
 * A signal handler can interrupt the main program at any moment, even while
 * the main program holds a stdio or malloc lock.  Using printf/malloc/etc.
 * inside the handler can deadlock or corrupt state.  write(2) is one of the
 * few POSIX functions guaranteed to be async-signal-safe, so we use it for
 * all handler output.
 *
 * Build:  cc -std=c11 -Wall -Wextra -O2 -g examples/08_hw_interrupt.c -o
 * 08_hw_interrupt Run:    ./08_hw_interrupt
 *
 * Platform notes
 * --------------
 * - Linux glibc x86_64:   uc_mcontext is embedded; regs via
 * gregs[REG_RIP/RSP/RBP].
 * - Linux glibc aarch64:  uc_mcontext is embedded; regs via .pc/.sp/.regs[29].
 * - macOS x86_64:         uc_mcontext is a POINTER; deref to
 * __ss.__rip/__rsp/__rbp.
 * - macOS ARM64 (Apple):  uc_mcontext is a POINTER; deref to
 * __ss.__pc/__sp/__fp.
 */

/*
 * _XOPEN_SOURCE is needed for ucontext_t on many systems.
 * _DARWIN_C_SOURCE ensures SIGSTKSZ / ucontext on older macOS versions.
 */
#ifndef _XOPEN_SOURCE
#define _XOPEN_SOURCE 700
#endif
#ifndef _DARWIN_C_SOURCE
#define _DARWIN_C_SOURCE
#endif

#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>
#include <ucontext.h>
#include <unistd.h>

/* Number of timer "interrupts" to demonstrate before disarming. */
#define MAX_INTERRUPTS 5

/* Counter incremented only by the signal handler.  sig_atomic_t guarantees
 * that read/write is atomic with respect to signal interruption. */
static volatile sig_atomic_t interrupt_count = 0;

/*
 * Async-signal-safe helpers for printing without printf().
 * We build messages in stack buffers and write them with write(2).
 */

/* Write a NUL-terminated string to stdout using write(2). */
static void safe_puts(const char* s) {
    size_t len = 0;
    while (s[len] != '\0') {
        ++len;
    }
    (void)write(STDOUT_FILENO, s, len);
}

#if (defined(__APPLE__) && defined(__MACH__) &&        \
     (defined(__x86_64__) || defined(__aarch64__))) || \
    (defined(__linux__) && (defined(__x86_64__) || defined(__aarch64__)))
/* Write a single hex digit. */
static void safe_hexdigit(int n) {
    char c = (char)((n < 10) ? ('0' + n) : ('a' + (n - 10)));
    (void)write(STDOUT_FILENO, &c, 1);
}

/* Write a uintptr_t value in hexadecimal with a fixed 0x prefix. */
static void safe_hex(uintptr_t val) {
    safe_puts("0x");
    for (int i = (int)(sizeof(val) * 8 - 4); i >= 0; i -= 4) {
        safe_hexdigit((int)((val >> (unsigned)i) & 0xF));
    }
}
#endif

/*
 * The signal handler — our "Interrupt Service Routine".
 *
 * The third argument, void *uctx_ptr, is a pointer to ucontext_t.
 * ucontext_t contains the saved register state of the interrupted context,
 * exactly like a hardware trap frame.
 */
static void sigvtalrm_isr(int sig, siginfo_t* info, void* uctx_ptr) {
    (void)info;

    /* Cast the opaque third argument to the concrete context type. */
    ucontext_t* uctx = (ucontext_t*)uctx_ptr;

    /*
     * Platform-specific extraction of the saved registers.
     * These values are the *interrupted* main-loop state.
     */
#if defined(__APPLE__) && defined(__MACH__) && defined(__x86_64__)
    /*
     * macOS/Darwin x86_64:
     * uc_mcontext is a pointer (mcontext_t).  Dereference it to reach the
     * __darwin_mcontext64 structure, then read the x86_64 saved state.
     */
    uintptr_t pc = 0, sp = 0, fp = 0;
    if (uctx != NULL && uctx->uc_mcontext != NULL) {
        pc = (uintptr_t)(uctx->uc_mcontext->__ss.__rip);
        sp = (uintptr_t)(uctx->uc_mcontext->__ss.__rsp);
        fp = (uintptr_t)(uctx->uc_mcontext->__ss.__rbp);
    }
#elif defined(__APPLE__) && defined(__MACH__) && defined(__aarch64__)
    /*
     * macOS/Darwin ARM64 (Apple Silicon):
     * uc_mcontext is a pointer (mcontext_t).  Dereference it to reach
     * __darwin_arm_thread_state64, then read pc/sp/fp/lr.
     */
    uintptr_t pc = 0, sp = 0, fp = 0;
    if (uctx != NULL && uctx->uc_mcontext != NULL) {
        pc = (uintptr_t)(uctx->uc_mcontext->__ss.__pc);
        sp = (uintptr_t)(uctx->uc_mcontext->__ss.__sp);
        fp = (uintptr_t)(uctx->uc_mcontext->__ss.__fp);
    }
#elif defined(__linux__) && defined(__x86_64__)
    /*
     * Linux glibc x86_64:
     * uc_mcontext is an embedded mcontext_t.  gregs[] holds the general
     * purpose registers; REG_RIP/REG_RSP/REG_RBP are defined in
     * sys/ucontext.h.
     */
    uintptr_t pc = 0, sp = 0, fp = 0;
    if (uctx != NULL) {
        pc = (uintptr_t)(uctx->uc_mcontext.gregs[REG_RIP]);
        sp = (uintptr_t)(uctx->uc_mcontext.gregs[REG_RSP]);
        fp = (uintptr_t)(uctx->uc_mcontext.gregs[REG_RBP]);
    }
#elif defined(__linux__) && defined(__aarch64__)
    /*
     * Linux ARM64 (aarch64):
     * uc_mcontext has pc, sp, regs[29] (frame pointer), regs[30] (link reg).
     */
    uintptr_t pc = 0, sp = 0, fp = 0;
    if (uctx != NULL) {
        pc = (uintptr_t)(uctx->uc_mcontext.pc);
        sp = (uintptr_t)(uctx->uc_mcontext.sp);
        fp = (uintptr_t)(uctx->uc_mcontext.regs[29]);
    }
#else
    /* Other platforms: ucontext layout varies.  We print a banner
     * to show the interrupt fired, but cannot decode registers portably. */
    uintptr_t pc = 0, sp = 0, fp = 0;
    (void)uctx;
#endif

    /*
     * Print the "interrupt" using only write(2).  This is the only safe way
     * to produce output from a signal handler.
     */
    safe_puts("\n=== Timer ISR (SIGVTALRM) ===\n");
    safe_puts("Signal number: ");
    {
        char nbuf[4] = {0};
        int n = sig;
        int pos = 0;
        if (n >= 100)
            nbuf[pos++] = (char)('0' + (n / 100));
        if (n >= 10)
            nbuf[pos++] = (char)('0' + ((n / 10) % 10));
        nbuf[pos++] = (char)('0' + (n % 10));
        (void)write(STDOUT_FILENO, nbuf, (size_t)pos);
    }
    safe_puts("\n");

#if (defined(__APPLE__) && defined(__MACH__) &&        \
     (defined(__x86_64__) || defined(__aarch64__))) || \
    (defined(__linux__) && (defined(__x86_64__) || defined(__aarch64__)))
    safe_puts("Saved PC  (program counter) = ");
    safe_hex(pc);
    safe_puts("\n");

    safe_puts("Saved SP  (stack pointer)   = ");
    safe_hex(sp);
    safe_puts("\n");

    safe_puts("Saved FP  (frame pointer)   = ");
    safe_hex(fp);
    safe_puts("\n");
#else
    safe_puts(
        "Register dump: N/A (add platform-specific #ifdef for this arch)\n");
#endif

    safe_puts("==============================\n");

    ++interrupt_count;
}

/*
 * Arm a virtual-time interval timer.
 * ITIMER_VIRTUAL counts only user-mode CPU time consumed by this process.
 * Every 500 ms of CPU time, the kernel generates SIGVTALRM.
 */
static int arm_timer(unsigned long usec) {
    struct itimerval it;

    it.it_value.tv_sec = (time_t)(usec / 1000000UL);
    it.it_value.tv_usec = (suseconds_t)(usec % 1000000UL);

    /* Same interval for periodic firing. */
    it.it_interval = it.it_value;

    if (setitimer(ITIMER_VIRTUAL, &it, NULL) == -1) {
        return -1;
    }
    return 0;
}

/* Disarm the virtual timer. */
static int disarm_timer(void) {
    struct itimerval it;
    memset(&it, 0, sizeof(it));
    if (setitimer(ITIMER_VIRTUAL, &it, NULL) == -1) {
        return -1;
    }
    return 0;
}

int main(void) {
    struct sigaction sa;

    printf("=== Timer ISR vs POSIX Signal Handler ===\n");
    printf("This program demonstrates the 1-to-1 mapping between\n");
    printf("hardware interrupts / ISRs and POSIX signals / handlers.\n\n");
    printf(
        "Hardware interrupt:   IRQ -> CPU saves regs -> IDT -> ISR -> iret\n");
    printf(
        "POSIX signal:         setitimer -> kernel saves ucontext_t -> "
        "sigaction table -> handler -> sigreturn\n\n");
    printf("A busy CPU loop runs.  Every 500 ms of virtual CPU time,\n");
    printf("SIGVTALRM fires and the handler prints the saved PC/SP/FP.\n\n");
    fflush(stdout);

    /*
     * Step 1: install the SIGVTALRM handler with SA_SIGINFO.
     * SA_SIGINFO is required to receive the third argument (ucontext_t*).
     */
    memset(&sa, 0, sizeof(sa));
    sa.sa_sigaction = sigvtalrm_isr;
    if (sigemptyset(&sa.sa_mask) == -1) {
        perror("sigemptyset");
        return EXIT_FAILURE;
    }
    sa.sa_flags = SA_SIGINFO;

    if (sigaction(SIGVTALRM, &sa, NULL) == -1) {
        perror("sigaction");
        return EXIT_FAILURE;
    }

    /*
     * Step 2: arm the virtual timer.
     * 500,000 microseconds = 500 milliseconds.
     */
    if (arm_timer(500000UL) == -1) {
        perror("setitimer");
        return EXIT_FAILURE;
    }

    printf("Timer armed.  Entering busy loop...\n");
    fflush(stdout);

    /*
     * Step 3: busy CPU loop.
     * Because ITIMER_VIRTUAL only counts user CPU time, this loop will
     * accumulate virtual time and trigger SIGVTALRM repeatedly.
     * The handler runs asynchronously, just like a hardware ISR.
     */
    while (interrupt_count < MAX_INTERRUPTS) {
        /* A tiny amount of work so the loop does not optimize away. */
        volatile unsigned long counter = 0;
        for (unsigned long i = 0; i < 10000000UL; ++i) {
            ++counter;
        }

        /*
         * This printf runs in normal context, between timer interrupts.
         * It shows that the main program continues as if nothing happened,
         * while the signal handler fires in between iterations.
         */
        printf("[main loop] still running, interrupts so far = %d\n",
               (int)interrupt_count);
        fflush(stdout);
    }

    /*
     * Step 4: disarm the timer and exit.
     * This is analogous to masking or disabling a hardware interrupt source.
     */
    if (disarm_timer() == -1) {
        perror("setitimer disarm");
        return EXIT_FAILURE;
    }

    printf("\nTimer disarmed after %d interrupts.  Exiting cleanly.\n",
           (int)interrupt_count);
    return EXIT_SUCCESS;
}
