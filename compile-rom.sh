#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

# Use the space-free access symlink created by setup.sh. Toolchain bytes live
# in the ignored .n64-bootstrap directory inside this repository.
N64_BOOTSTRAP="${N64_BOOTSTRAP:-${HOME}/.n64-bootstrap}"
CANDIDATES=(
    "${N64_BOOTSTRAP}/opt/libdragon"
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
# N64_INST is the space-free access path; export it so libdragon tools
# (n64sym/mkfont/mkmaterial) find it via getenv(), and pass N64_BOOTSTRAP
# through so the Makefile uses the same location.
export N64_INST
export N64_BOOTSTRAP
export PATH="$N64_INST/bin:$PATH"

rm -rf madeline_cube_rom.z64 madeline_cube_rom.dfs

exec make "$@"
