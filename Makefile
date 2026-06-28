CC      ?= cc
CFLAGS  ?= -std=c11 -Wall -Wextra -O2 -g

# macOS: <ucontext.h> (used by 05/08/09) is marked deprecated but still works.
# Silence the -Wdeprecated-declarations noise on Darwin only.
# NOTE: feature-test macros (_DARWIN_C_SOURCE, _XOPEN_SOURCE, _GNU_SOURCE,
# _POSIX_C_SOURCE) are deliberately NOT set here.  Every .c file declares the
# macros it needs at the top, so each example also builds standalone with the
# bare "cc ..." command shown in its header comment.
ifeq ($(shell uname -s),Darwin)
	CFLAGS += -Wno-deprecated-declarations
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

%: $(EXAMPLES_DIR)/%.c
	$(CC) $(CFLAGS) $< -o $@ $(LDFLAGS)

run: $(BINS)
	@echo "=== Running 01_signal_basics ==="
	@timeout 3 ./01_signal_basics || true
	@echo ""
	@echo "=== Running 02_sigaction ==="
	@timeout 3 ./02_sigaction || true
	@echo ""
	@echo "=== Running 03_blocking ==="
	@timeout 3 ./03_blocking || true
	@echo ""
	@echo "=== Running 04_timer ==="
	@timeout 8 ./04_timer || true
	@echo ""
	@echo "=== Running 05_altstack ==="
	@timeout 3 ./05_altstack || true
	@echo ""
	@echo "=== Running 06_realtime ==="
	@timeout 3 ./06_realtime || true
	@echo ""
	@echo "=== Running 08_hw_interrupt ==="
	@timeout 3 ./08_hw_interrupt || true
	@echo ""
	@echo "=== Running 09_fork_exec ==="
	@timeout 3 ./09_fork_exec || true
	@echo ""
	@echo "=== Running 10_signal_safety ==="
	@timeout 8 ./10_signal_safety || true
	@echo ""
	@echo "=== All examples completed ==="
	@echo ""
	@echo "Note: 07_selfpipe is interactive (select() event loop)."
	@echo "      Run it manually:  make run-interactive   or   ./07_selfpipe"

# Interactive samples that block waiting for terminal input or external
# signals.  They cannot be auto-run by `make run`; launch them manually and
# send signals (Ctrl-C / kill -INT / kill -TERM) from another terminal.
run-interactive: 07_selfpipe
	@echo "=== 07_selfpipe (interactive) ==="
	@echo "PID will be printed by the program. From another terminal:"
	@echo "  kill -INT  <pid>     kill -TERM <pid>"
	@echo "Press Ctrl-C in this terminal to stop."
	@./07_selfpipe || true

format:
	@echo "Formatting all .c and .h files..."
	@find . \( -name "*.c" -o -name "*.h" \) -print0 | xargs -0 clang-format -i
	@echo "Formatting complete."

clean:
	rm -f $(BINS)
	rm -rf *.dSYM

.PHONY: all format clean run run-interactive
