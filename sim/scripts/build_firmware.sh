#!/usr/bin/env bash
# CI-002: Build the production firmware ELF via PlatformIO.
# Does NOT touch platformio.ini, does NOT run the migration script.
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "$SCRIPT_DIR/../.." && pwd)"
ELF="$REPO_ROOT/.pio/build/nava_sysex/firmware.elf"

echo "==> Building firmware (env: nava_sysex)"
cd "$REPO_ROOT"
pio run -e nava_sysex

if [[ ! -f "$ELF" ]]; then
    echo "ERROR: Expected ELF not found: $ELF" >&2
    exit 1
fi

SIZE=$(wc -c < "$ELF")
if [[ "$SIZE" -eq 0 ]]; then
    echo "ERROR: $ELF is empty" >&2
    exit 1
fi

echo "==> Firmware ELF ready: $ELF (${SIZE} bytes)"

# Print the embedded mmcu string so the operator can verify it.
# Note: the ELF mmcu string and simavr's core-registry name are independent
# identifiers and need not match textually (see nava_sim.c nava_sim_create).
MMCU=$(strings "$ELF" 2>/dev/null | grep -iE 'atmega|1284' | head -5 || true)
echo "==> ELF mmcu hint: ${MMCU:-not found in strings output}"
