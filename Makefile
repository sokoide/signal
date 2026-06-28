CC      ?= cc
CFLAGS  ?= -std=c11 -Wall -Wextra -O2 -g

# macOS: ucontext API is marked deprecated in 10.6+ but still works.
# Silence the noise on Darwin only.
ifeq ($(shell uname -s),Darwin)
	CFLAGS += -Wno-deprecated-declarations
endif

# macOS pre-11 may not expose SIGSTKSZ without this.
ifeq ($(shell uname -s),Darwin)
	CFLAGS += -D_DARWIN_C_SOURCE
endif

# macOS bundles real-time functions (timer_create, etc.) in libc.
# Linux requires -lrt.
ifeq ($(shell uname -s),Linux)
	LDFLAGS ?= -lrt
else
	LDFLAGS ?=
endif

EXAMPLES_DIR := examples

BINS := 01_signal_basics \
        02_sigaction \
        03_blocking \
        04_timer \
        05_altstack \
        06_realtime \
        07_selfpipe \
        08_hw_interrupt \
        09_fork_exec \
        10_signal_safety

all: $(BINS)

01_signal_basics: $(EXAMPLES_DIR)/01_signal_basics.c
	$(CC) $(CFLAGS) $< -o $@ $(LDFLAGS)

02_sigaction: $(EXAMPLES_DIR)/02_sigaction.c
	$(CC) $(CFLAGS) $< -o $@ $(LDFLAGS)

03_blocking: $(EXAMPLES_DIR)/03_blocking.c
	$(CC) $(CFLAGS) $< -o $@ $(LDFLAGS)

04_timer: $(EXAMPLES_DIR)/04_timer.c
	$(CC) $(CFLAGS) $< -o $@ $(LDFLAGS)

05_altstack: $(EXAMPLES_DIR)/05_altstack.c
	$(CC) $(CFLAGS) $< -o $@ $(LDFLAGS)

06_realtime: $(EXAMPLES_DIR)/06_realtime.c
	$(CC) $(CFLAGS) $< -o $@ $(LDFLAGS)

07_selfpipe: $(EXAMPLES_DIR)/07_selfpipe.c
	$(CC) $(CFLAGS) $< -o $@ $(LDFLAGS)

08_hw_interrupt: $(EXAMPLES_DIR)/08_hw_interrupt.c
	$(CC) $(CFLAGS) $< -o $@ $(LDFLAGS)

09_fork_exec: $(EXAMPLES_DIR)/09_fork_exec.c
	$(CC) $(CFLAGS) $< -o $@ $(LDFLAGS)

10_signal_safety: $(EXAMPLES_DIR)/10_signal_safety.c
	$(CC) $(CFLAGS) $< -o $@ $(LDFLAGS)

format:
	@echo "Formatting all .c and .h files..."
	@find . -name "*.c" -o -name "*.h" | xargs clang-format -i
	@echo "Formatting complete."

clean:
	rm -f $(BINS)
	rm -rf *.dSYM

.PHONY: all format clean
