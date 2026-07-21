# Signal tutorial targets. Included by the repository root Makefile.
TUT_SIG_DIR := tutorial/signal
TUT_SIG_STEPS := step01_basics step02_sigaction step03_mask_pending step04_sigsuspend \
	step05_selfpipe step06_sigwait
TUT_SIG_BINS := $(addprefix $(TUT_SIG_DIR)/,$(TUT_SIG_STEPS))
TUT_SIG_TESTS := $(addprefix $(TUT_SIG_DIR)/tests/,step01_contract_test step02_contract_test \
	step03_contract_test step04_contract_test step05_contract_test step06_contract_test)

.PHONY: run-tutorial-signal test-tutorial-signal clean-tutorial-signal \
	test-tutorial-signal-01 test-tutorial-signal-02 \
	test-tutorial-signal-03 test-tutorial-signal-04 \
	test-tutorial-signal-05 test-tutorial-signal-06

$(TUT_SIG_DIR)/step01_basics: $(TUT_SIG_DIR)/step01_basics.c
$(TUT_SIG_DIR)/step02_sigaction: $(TUT_SIG_DIR)/step02_sigaction.c
$(TUT_SIG_DIR)/step03_mask_pending: $(TUT_SIG_DIR)/step03_mask_pending.c
$(TUT_SIG_DIR)/step04_sigsuspend: $(TUT_SIG_DIR)/step04_sigsuspend.c
$(TUT_SIG_DIR)/step05_selfpipe: $(TUT_SIG_DIR)/step05_selfpipe.c
$(TUT_SIG_DIR)/step06_sigwait: $(TUT_SIG_DIR)/step06_sigwait.c
$(TUT_SIG_DIR)/step%: $(TUT_SIG_DIR)/step%.c
	$(CC) $(CFLAGS) $< -o $@ $(LDFLAGS)

$(TUT_SIG_DIR)/tests/%_contract_test: $(TUT_SIG_DIR)/tests/%_contract_test.c
	$(CC) $(CFLAGS) $< -o $@ $(LDFLAGS)

run-tutorial-signal: $(TUT_SIG_BINS)
	@for b in $(TUT_SIG_BINS); do echo "=== $$b ==="; timeout 5 ./$$b || exit $$?; done

test-tutorial-signal-01: $(TUT_SIG_DIR)/step01_basics $(TUT_SIG_DIR)/tests/step01_contract_test
	@timeout 5 ./$(TUT_SIG_DIR)/tests/step01_contract_test

test-tutorial-signal-02: $(TUT_SIG_DIR)/step02_sigaction $(TUT_SIG_DIR)/tests/step02_contract_test
	@timeout 5 ./$(TUT_SIG_DIR)/tests/step02_contract_test

test-tutorial-signal-03: $(TUT_SIG_DIR)/step03_mask_pending $(TUT_SIG_DIR)/tests/step03_contract_test
	@timeout 5 ./$(TUT_SIG_DIR)/tests/step03_contract_test

test-tutorial-signal-04: $(TUT_SIG_DIR)/step04_sigsuspend $(TUT_SIG_DIR)/tests/step04_contract_test
	@timeout 5 ./$(TUT_SIG_DIR)/tests/step04_contract_test

test-tutorial-signal-05: $(TUT_SIG_DIR)/step05_selfpipe $(TUT_SIG_DIR)/tests/step05_contract_test
	@timeout 5 ./$(TUT_SIG_DIR)/tests/step05_contract_test

test-tutorial-signal-06: $(TUT_SIG_DIR)/step06_sigwait $(TUT_SIG_DIR)/tests/step06_contract_test
	@timeout 5 ./$(TUT_SIG_DIR)/tests/step06_contract_test

test-tutorial-signal: $(TUT_SIG_BINS) $(TUT_SIG_TESTS)
	@set -e; for t in $(TUT_SIG_TESTS); do echo "=== $$t ==="; timeout 5 ./$$t; done

clean-tutorial-signal:
	rm -f $(TUT_SIG_BINS) $(TUT_SIG_TESTS)
	rm -rf $(TUT_SIG_DIR)/*.dSYM $(TUT_SIG_DIR)/tests/*.dSYM
