# Tests — OrbStack dual-architecture Linux

The samples are verified by actually running them on **Linux**, on **two
architectures** (`aarch64` and `x86_64`), via [OrbStack](https://orbstack.dev)
machines. macOS alone cannot exercise `06_realtime` (POSIX real-time signals
are unsupported on Darwin), and running on both architectures catches
arch-dependent behaviour such as the `ucontext_t` register dump in
`08_hw_interrupt` (`RIP/RSP/RBP` on x86_64 vs `PC/SP/FP` on aarch64).

## Prerequisites

- OrbStack installed and `orb` / `orbctl` on PATH.

## One-time setup

```sh
make linux-machines   # creates arm64-linux-env and x64-linux-env (ubuntu:24.04)
make linux-setup      # installs build-essential in both (runs as root in the VM)
```

## Run the tests

```sh
make check            # build + run all samples on both arches, diff vs expected
```

`make check` also runs `make run` inside each machine as a smoke test (the
whole suite must complete without hanging or crashing). On success the only
output is `ALL PASS (aarch64 + x86_64)`; on a mismatch it prints the `diff -u`
and exits non-zero.

### Verbose mode: see exactly what is verified

```sh
make check V=1
```

`V=1` surfaces everything the harness does inside each machine, in order:

1. the full build log (`cc ...` lines for every sample),
2. the `make run` smoke output (raw, with real PIDs),
3. the **normalized output** of each sample (PIDs/UIDs/addresses → `<PID>` etc.),
4. a `PASS <arch>/NN.txt` line per sample, and finally `ALL PASS`.

This is the recommended way to confirm what the green `ALL PASS` actually
proves.

## What gets verified

Each sample's normalized output is compared byte-for-byte. Highlights:

- **`06_realtime`** — standard signals (`SIGUSR1`) sent 5× merge into a single
  delivery, while queued real-time signals (`SIGRTMIN`, `SIGRTMIN+1`) are all
  delivered in priority order with their `sigqueue` data intact (15 events).
- **`08_hw_interrupt`** — `SIGVTALRM` fires 5×; each time the handler dumps the
  saved `PC`/`SP`/`FP` (aarch64) or `RIP`/`RSP`/`RBP` (x86_64) from
  `ucontext_t`, demonstrating the structural correspondence between an ISR and
  a POSIX signal handler. Addresses normalize to `0x<ADDR>`.
- **`07_selfpipe`** — the harness holds an empty FIFO on stdin so `select()`
  blocks, then injects `SIGINT`; the only exit path is the self-pipe, proving
  the signal-to-fd conversion and clean shutdown.
- **`01_signal_basics`** — intentionally ends with `raise(SIGINT)` under
  `SIG_DFL` (killed by signal, rc 130), treated as success.

## SIGSTKSZ is normalized (libc-independent expected outputs)

`05_altstack` and `09_fork_exec` print the alternate-stack size, which is
`SIGSTKSZ`:

```
aarch64: size 20480 bytes      # glibc SIGSTKSZ on ARM64
x86_64:  size 8192  bytes      # glibc SIGSTKSZ on x86-64
```

`SIGSTKSZ` is libc-dependent (glibc 2.34+ evaluates it at runtime; musl uses a
different value), so the expected outputs normalize it to `<SIGSTKSZ>` via
`scripts/normalize.sed`. This makes the committed outputs reproducible across
distros/libcs rather than pinning them to one build environment's value. All
10 samples therefore match byte-for-byte across both architectures after
normalization.

## Expected outputs

Committed under `tests/expected/<arch>/NN.txt`, one per sample, **normalized**
(see `scripts/normalize.sed`):

- PIDs / UIDs → `<PID>` / `<UID>`
- hex addresses (ASLR, arch pointer width) → `0x<ADDR>`
- CPU-speed-dependent timer counts (04 busy loop) → `<N>`

### Regenerate

When an output change is intentional (code or message edit):

```sh
make expected         # rewrites tests/expected/{aarch64,x86_64}/*.txt
# review the git diff, then commit
```

## Convenience aliases (interactive work)

```sh
alias orb-arm64='cd /Users/scott/repo/sokoide/signal && orb -m arm64-linux-env'
alias orb-x64='cd /Users/scott/repo/sokoide/signal && orb -m x64-linux-env'
# SSH access:
#   ssh arm64-linux-env@orb
#   ssh x64-linux-env@orb
```

## Files

| Path | Role |
|------|------|
| `scripts/in-linux.sh` | Run a command in a machine, from the host repo root |
| `scripts/linux-setup.sh` | Install build tools in both machines |
| `scripts/check.sh` | Host orchestrator (loop machines, diff) |
| `scripts/check-linux.sh` | In-machine: build, run, normalize |
| `scripts/normalize.sed` | Volatile-value normalization rules |

## Notes

- The dedicated machines (`arm64-linux-env`, `x64-linux-env`) are separate from
  any other OrbStack machines you may have, to avoid polluting them.
- `07_selfpipe` is interactive; the harness launches it and injects `SIGINT`.
- `01_signal_basics` intentionally exits via `raise(SIGINT)` under `SIG_DFL`
  (killed by signal) — the harness treats that as success.
