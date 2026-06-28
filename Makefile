CC      ?= cc
CFLAGS  ?= -std=c11 -Wall -Wextra -O2 -g

# macOS: ucontext API is marked deprecated in 10.6+ but still works.
# Silence the noise on Darwin only. Also expose SIGSTKSZ on older versions.
ifeq ($(shell uname -s),Darwin)
	CFLAGS += -Wno-deprecated-declarations -D_DARWIN_C_SOURCE
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
	@timeout 3 ./04_timer || true
	@echo ""
	@echo "=== Running 05_altstack ==="
	@timeout 3 ./05_altstack || true
	@echo ""
	@echo "=== Running 06_realtime ==="
	@timeout 3 ./06_realtime || true
	@echo ""
	@echo "=== Running 07_selfpipe ==="
	@timeout 3 ./07_selfpipe || true
	@echo ""
	@echo "=== Running 08_hw_interrupt ==="
	@timeout 3 ./08_hw_interrupt || true
	@echo ""
	@echo "=== Running 09_fork_exec ==="
	@timeout 3 ./09_fork_exec || true
	@echo ""
	@echo "=== Running 10_signal_safety ==="
	@timeout 3 ./10_signal_safety || true
	@echo ""
	@echo "=== All examples completed ==="

format:
	@echo "Formatting all .c and .h files..."
	@find . \( -name "*.c" -o -name "*.h" \) -print0 | xargs -0 clang-format -i
	@echo "Formatting complete."

clean:
	rm -f $(BINS)
	rm -rf *.dSYM

.PHONY: all format clean run
