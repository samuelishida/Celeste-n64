#!/usr/bin/env bash
set -euo pipefail

# Try known /tmp bootstrap locations in order
CANDIDATES=(
    /tmp/n64-bootstrap/opt/libdragon
    /tmp/n64-toolchain-root/opt/libdragon
    /tmp/toolchain_extract/opt/libdragon
)

N64_INST=""
for candidate in "${CANDIDATES[@]}"; do
    if [[ -f "$candidate/include/n64.mk" ]]; then
        N64_INST="$candidate"
        break
    fi
done

if [[ -z "$N64_INST" ]]; then
    echo "ERROR: no libdragon toolchain found. Expected one of:"
    for candidate in "${CANDIDATES[@]}"; do
        echo "  $candidate"
    done
    echo ""
    echo "Run the cold-start bootstrap from AGENTS.md, then retry."
    exit 1
fi

echo "Using toolchain: $N64_INST"
export N64_INST
export PATH="$N64_INST/bin:$PATH"

exec make "$@"
