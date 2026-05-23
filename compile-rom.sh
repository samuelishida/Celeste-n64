#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

# Primary: /tmp/n64-bootstrap (setup.sh installs here — ext4, supports symlinks)
# Fallback: other /tmp locations from older bootstraps
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
    echo "ERROR: no libdragon toolchain found. Checked:"
    for candidate in "${CANDIDATES[@]}"; do
        echo "  $candidate"
    done
    echo ""
    echo "Run the bootstrap script to set up the toolchain:"
    echo "  ${SCRIPT_DIR}/setup.sh"
    exit 1
fi

echo "Using toolchain: $N64_INST"
export N64_INST
export PATH="$N64_INST/bin:$PATH"

rm -rf madeline_cube_rom.z64 madeline_cube_rom.dfs

exec make "$@"
