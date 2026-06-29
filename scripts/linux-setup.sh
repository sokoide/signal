#!/usr/bin/env bash
# Install build tooling in both OrbStack Linux machines used for testing.
#
# Prerequisite: the machines must already exist (run `make linux-machines`).
# OrbStack's default user is assumed to have passwordless sudo; if not, sudo
# will prompt for a password interactively.
set -euo pipefail

cd "$(dirname "${BASH_SOURCE[0]}")"

for m in arm64-linux-env x64-linux-env; do
    echo ">>> $m: installing build-essential"
    # in-linux.sh runs as root inside the VM, so no sudo is needed.
    ./in-linux.sh "$m" \
        'apt-get update && DEBIAN_FRONTEND=noninteractive apt-get install -y build-essential'
done

echo ">>> done. Verify:"
for m in arm64-linux-env x64-linux-env; do
    printf '%-18s ' "$m"
    ./in-linux.sh "$m" 'cc --version | head -1'
done
