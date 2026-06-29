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
whole suite must complete without hanging or crashing).

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
