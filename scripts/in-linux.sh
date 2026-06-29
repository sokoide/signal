#!/usr/bin/env bash
# Run a command inside an OrbStack Linux machine, from the host repo root.
#
# OrbStack mounts the macOS filesystem at the same absolute path inside every
# machine, so the host repository is edited/built/run in place — no copy.
#
# Usage: scripts/in-linux.sh <machine> <command...>
# Example:
#   scripts/in-linux.sh x64-linux-env make
#   scripts/in-linux.sh arm64-linux-env ./06_realtime
set -euo pipefail

if [ "$#" -lt 2 ]; then
    echo "usage: $0 <machine> <command...>" >&2
    exit 2
fi

machine="$1"
shift

# Make sure the machine is up (no-op if already running).
orbctl start "$machine" >/dev/null 2>&1 || true

# Resolve the repo root on the host; the same path is used inside the VM.
repo="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"

# Run as root inside the VM. OrbStack maps Linux root to the macOS owner of
# the mounted filesystem, so files written from the VM are owned by the host
# user — no ownership surprises, and no sudo needed for package installs.
# The command is subject to remote shell parsing, so keep arguments simple
# (no embedded quotes/spaces).
orb -m "$machine" -u root bash -lc "cd '$repo' && $*"
