CC      ?= cc
CFLAGS  ?= -std=c11 -Wall -Wextra -O2 -g
TIMEOUT ?= $(shell command -v gtimeout 2>/dev/null || command -v timeout 2>/dev/null)

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

COMPLETED_DIR := completed/signal

BINS := 01_signal_basics \
        02_sigaction \
        03_blocking \
        04_timer \
        05_altstack \
        06_realtime \
        07_selfpipe \
        08_hw_interrupt \
        09_fork_exec \
        10_signal_safety \
        91_printf_deadlock

all: completed

completed: $(BINS)

run-completed: run

%: $(COMPLETED_DIR)/%.c
	$(CC) $(CFLAGS) $< -o $@ $(LDFLAGS)

# 91_printf_deadlock uses pthreads. macOS and glibc >= 2.34 ship pthread in
# libc, so -lpthread is unnecessary (and harmless) there; older glibc needs it.
# Built by `make` / `make 91_printf_deadlock`, but NOT by `make run` (it hangs).
91_printf_deadlock: $(COMPLETED_DIR)/91_printf_deadlock.c
	$(CC) $(CFLAGS) $< -o $@ $(LDFLAGS) -lpthread

check-timeout:
	@if [ -z "$(TIMEOUT)" ]; then \
		echo "error: GNU timeout is required (macOS: brew install coreutils)" >&2; \
		exit 127; \
	fi

# Run one self-terminating sample and reject unexpected exits. 01 terminates
# itself with SIGINT (128 + 2), while 05 exits from its SIGSEGV handler with
# 128 + SIGSEGV. Every other automated sample must return zero.
define run_sample
	@echo "=== Running $(1) ==="
	@set +e; "$(TIMEOUT)" $(2) ./$(1); rc=$$?; set -e; \
	case "$(1):$$rc" in \
		01_signal_basics:130|05_altstack:139|*:0) ;; \
		*) echo "ERROR: $(1) exited unexpectedly (rc=$$rc)" >&2; exit $$rc ;; \
	esac
	@echo ""
endef

run: check-timeout $(BINS)
	$(call run_sample,01_signal_basics,3)
	$(call run_sample,02_sigaction,3)
	$(call run_sample,03_blocking,3)
	$(call run_sample,04_timer,8)
	$(call run_sample,05_altstack,3)
	$(call run_sample,06_realtime,3)
	$(call run_sample,08_hw_interrupt,3)
	$(call run_sample,09_fork_exec,3)
	$(call run_sample,10_signal_safety,8)
	@echo "=== All examples completed ==="
	@echo ""
	@echo "Note: 07_selfpipe is interactive (select() event loop)."
	@echo "      Run it manually:  make run-interactive   or   ./07_selfpipe"
	@echo "Note: 91_printf_deadlock intentionally DEADLOCKS (hangs forever)."
	@echo "      It is built by 'make' but NOT run by 'make run'."
	@echo "      Run it manually with a timeout:  timeout 5 ./91_printf_deadlock"

# Interactive samples that block waiting for terminal input or external
# signals.  They cannot be auto-run by `make run`; launch them manually and
# send signals (Ctrl-C / kill -INT / kill -TERM) from another terminal.
run-interactive: 07_selfpipe
	@echo "=== 07_selfpipe (interactive) ==="
	@echo "PID will be printed by the program. From another terminal:"
	@echo "  kill -INT  <pid>     kill -TERM <pid>"
	@echo "Press Ctrl-C in this terminal to stop."
	@./07_selfpipe || true

# ---------------------------------------------------------------------------
# Local Linux testing on OrbStack (aarch64 + x86_64).
# See completed/tests/README.md for the full workflow. macOS stays the human-facing host.
# ---------------------------------------------------------------------------
LINUX_MACHINES := arm64-linux-env x64-linux-env

# Create the two dedicated machines (idempotent: skips ones that already exist).
linux-machines:
	@for spec in "arm64 arm64-linux-env" "amd64 x64-linux-env"; do \
		set -- $$spec; \
		arch=$$1; name=$$2; \
		if orbctl list | awk '{print $$1}' | grep -qx "$$name"; then \
			echo "[skip] machine $$name already exists"; \
		else \
			echo "[create] $$name ($$arch)"; \
			orbctl create -a $$arch ubuntu:24.04 $$name; \
		fi; \
	done

# Install build-essential in both machines (run once after linux-machines).
linux-setup:
	@scripts/linux-setup.sh

# Regenerate the committed expected outputs on both architectures.
# Review the diff and commit when an output change is intentional.
expected:
	@scripts/check.sh generate

# Build, run all samples on both machines, and diff against completed/tests/expected/.
# Append V=1 for verbose output (per-sample build/run logs, normalized
# sample output, and a PASS line per sample):  make check V=1
check:
	@scripts/check.sh

format:
	@echo "Formatting all .c and .h files..."
	@find . \( -name "*.c" -o -name "*.h" \) -print0 | xargs -0 clang-format -i
	@echo "Formatting complete."

clean:
	rm -f $(BINS)
	$(MAKE) clean-tutorial-signal
	rm -rf *.dSYM

include tutorial/targets.mk

.PHONY: all completed format clean check-timeout run run-completed run-interactive linux-machines linux-setup expected check
