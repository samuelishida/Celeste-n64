#!/usr/bin/env bash
set -euo pipefail

# Madeline Cube ROM - Toolchain Bootstrap
#
# The repo lives on an exFAT partition that cannot hold symlinks,
# so the toolchain is installed to /tmp/n64-bootstrap (ext4).
# This survives reboots on most Linux setups, but can be lost if
# /tmp is cleaned. Re-run this script to rebuild if that happens.

N64_BOOTSTRAP="/tmp/n64-bootstrap"
N64_INST="${N64_BOOTSTRAP}/opt/libdragon"

echo "=== Madeline Cube ROM Toolchain Bootstrap ==="
echo ""
echo "Install target: ${N64_BOOTSTRAP}"
echo "(ext4 /tmp — required because the repo is on exFAT which lacks symlink support)"
echo ""

# Quick check: is everything already built?
if [[ -f "${N64_INST}/include/n64.mk" ]] && [[ -f "${N64_INST}/include/t3d.mk" ]] && [[ -f "${N64_INST}/lib/libdragon.a" ]]; then
    echo "✓ Toolchain already installed at ${N64_INST}"
    echo ""
    echo "You can now run: ./compile-rom.sh"
    exit 0
fi

# ── Step 1: Download GCC toolchain ──────────────────────────────────
TOOLCHAIN_DEB="${N64_BOOTSTRAP}/gcc-toolchain-mips64-x86_64.deb"
if [[ ! -f "${TOOLCHAIN_DEB}" ]]; then
    echo "Step 1: Downloading GCC MIPS64 toolchain..."
    mkdir -p "${N64_BOOTSTRAP}"
    curl -L \
      https://github.com/DragonMinded/libdragon/releases/download/toolchain-continuous-prerelease/gcc-toolchain-mips64-x86_64.deb \
      -o "${TOOLCHAIN_DEB}"
    echo "✓ Downloaded"
else
    echo "Step 1: GCC toolchain deb already present"
fi

# ── Step 2: Extract toolchain ───────────────────────────────────────
if [[ ! -f "${N64_INST}/bin/mips64-elf-gcc" ]]; then
    echo "Step 2: Extracting toolchain..."
    mkdir -p "${N64_INST}"
    dpkg-deb -x "${TOOLCHAIN_DEB}" "${N64_BOOTSTRAP}"
    echo "✓ Extracted to ${N64_INST}"
else
    echo "Step 2: Toolchain already extracted"
fi

export N64_INST
# libdragon's build.sh needs mips64-elf-gcc (in bin/) in PATH,
# but must NOT shadow the host gcc/cc (needed to build host-side tools).
# Strategy: put N64 bin/ at the END of PATH so host compilers take priority,
# but the cross-compiler is still discoverable.
export PATH="$PATH:${N64_INST}/bin:${N64_INST}/mips64-elf/bin"
export LD_LIBRARY_PATH="${N64_INST}/lib:${N64_INST}/lib/gcc/mips64-elf/14.2.0${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"

# ── Step 3: Clone & build libdragon ────────────────────────────────
LIBDRAGON_SRC="${N64_BOOTSTRAP}/libdragon"
if [[ ! -f "${N64_INST}/lib/libdragon.a" ]]; then
    echo "Step 3: Cloning & building libdragon (preview branch)..."
    if [[ ! -d "${LIBDRAGON_SRC}" ]]; then
        git clone --depth 1 --branch preview \
          https://github.com/DragonMinded/libdragon.git \
          "${LIBDRAGON_SRC}"
    fi
    cd "${LIBDRAGON_SRC}"
    ./build.sh
    echo "✓ libdragon installed"
else
    echo "Step 3: libdragon already built"
fi

# ── Step 4: Clone & build tiny3d ────────────────────────────────────
TINY3D_SRC="${N64_BOOTSTRAP}/tiny3d"
if [[ ! -f "${N64_INST}/include/t3d.mk" ]]; then
    echo "Step 4: Cloning & building tiny3d..."
    if [[ ! -d "${TINY3D_SRC}" ]]; then
        git clone --depth 1 \
          https://github.com/HailToDodongo/tiny3d.git \
          "${TINY3D_SRC}"
    fi
    cd "${TINY3D_SRC}"
    ./build.sh
    echo "✓ tiny3d installed"
else
    echo "Step 4: tiny3d already built"
fi

echo ""
echo "=== Bootstrap Complete ==="
echo ""
echo "Toolchain: ${N64_INST}"
echo ""
echo "You can now run: ./compile-rom.sh"
