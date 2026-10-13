#!/usr/bin/env bash
set -euo pipefail

# Madeline Cube ROM - Toolchain Bootstrap
#
# The toolchain bytes are stored in-repo (.n64-bootstrap, git-ignored) so they
# survive /tmp cleanup, but they are ACCESSED through a space-free symlink
# (default ~/.n64-bootstrap): libdragon tools (n64sym/mkfont) build shell
# commands from the toolchain path and break on spaces, and this repo lives
# under ".../Celeste 64 Demake". Re-run this script if the toolchain is missing.
# Set N64_BOOTSTRAP to relocate the access path.

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
N64_BOOTSTRAP_STORE="${SCRIPT_DIR}/.n64-bootstrap"
N64_BOOTSTRAP="${N64_BOOTSTRAP:-${HOME}/.n64-bootstrap}"
N64_INST="${N64_BOOTSTRAP}/opt/libdragon"
BLENDER_VERSION="4.0.2"
BLENDER_ARCHIVE="${N64_BOOTSTRAP}/blender-${BLENDER_VERSION}-linux-x64.tar.xz"
BLENDER_DIR="${N64_BOOTSTRAP}/blender-${BLENDER_VERSION}-linux-x64"
BLENDER_BIN="${BLENDER_DIR}/blender"
BLENDER_URL="https://download.blender.org/release/Blender4.0/blender-${BLENDER_VERSION}-linux-x64.tar.xz"
BLENDER_SHA256="5583a5588736da8858c522ef17fff5d73be59c47a6fe91ad29c6f3263e22086a"
ASSET_VENV="${N64_BOOTSTRAP}/asset-venv"
FAST64_DIR="${N64_BOOTSTRAP}/fast64"
FAST64_COMMIT="ba1249fd1c8de7e33cfb7aacc01d653af117f843"
# Pinned toolchain sources. Everything fetched over the network is pinned and
# verified so a re-published upstream asset or a moving branch cannot inject
# code into the build host. Keep in sync with tools/asset_toolchain.lock.json.
LIBDRAGON_COMMIT="39d0d6096130836a65710da6730290701d060272"
TINY3D_COMMIT="6f5ac0ba098f86d04e549ea754834f1be3c150e1"
LIBDRAGON_SRC="${N64_BOOTSTRAP}/libdragon"
TINY3D_SRC="${N64_BOOTSTRAP}/tiny3d"
GCC_TOOLCHAIN_SHA256="fac8e6572493a66468b7d45df41b042f90b1a630ec96aa218bd5fb1f320a0a4d"

if [[ "${N64_BOOTSTRAP}" =~ [[:space:]] ]]; then
    echo "ERROR: N64_BOOTSTRAP access path must not contain whitespace: ${N64_BOOTSTRAP}" >&2
    exit 1
fi

# Materialize the in-repo store and (re)create the space-free access symlink.
mkdir -p "${N64_BOOTSTRAP_STORE}"
if [[ "${N64_BOOTSTRAP_STORE}" != "${N64_BOOTSTRAP}" ]]; then
    if [[ -e "${N64_BOOTSTRAP}" && ! -L "${N64_BOOTSTRAP}" ]]; then
        echo "ERROR: ${N64_BOOTSTRAP} exists and is not the bootstrap symlink" >&2
        echo "Move it aside, then rerun setup.sh" >&2
        exit 1
    fi
    ln -sfnT "${N64_BOOTSTRAP_STORE}" "${N64_BOOTSTRAP}"
fi

repo_at_commit() {
    local repo="$1" expected="$2" actual status
    [[ -d "${repo}/.git" ]] || return 1
    actual="$(git -C "${repo}" rev-parse --verify HEAD 2>/dev/null)" || return 1
    [[ "${actual}" == "${expected}" ]] || return 1
    status="$(git -C "${repo}" status --porcelain --untracked-files=normal 2>/dev/null)" || return 1
    [[ -z "${status}" ]]
}

run_build_with_logical_dir() {
    local logical_dir="$1"
    local extra_makeflags="$2"
    shift 2
    local inherited_makeflags="${MAKEFLAGS:-}"
    local make_overrides="CURDIR=${logical_dir}"
    if [[ -n "${extra_makeflags}" ]]; then
        make_overrides+=" ${extra_makeflags}"
    fi
    MAKEFLAGS="${inherited_makeflags:+${inherited_makeflags} }${make_overrides}" "$@"
}

ensure_asset_tools() {
    mkdir -p "${N64_BOOTSTRAP}"
    if [[ ! -x "${BLENDER_BIN}" ]]; then
        echo "Asset step: downloading Blender ${BLENDER_VERSION}..."
        if [[ ! -f "${BLENDER_ARCHIVE}" ]]; then
            curl -L --fail --retry 3 "${BLENDER_URL}" -o "${BLENDER_ARCHIVE}"
        fi
        actual_sha="$(sha256sum "${BLENDER_ARCHIVE}" | awk '{print $1}')"
        [[ "${actual_sha}" == "${BLENDER_SHA256}" ]] || { echo "Blender checksum mismatch" >&2; return 1; }
        rm -rf "${BLENDER_DIR}"
        tar -xJf "${BLENDER_ARCHIVE}" -C "${N64_BOOTSTRAP}"
    fi
    blender_version="$("${BLENDER_BIN}" --version | sed -n '1p')"
    [[ "${blender_version}" == *"Blender ${BLENDER_VERSION}"* ]] || { echo "wrong Blender version: ${blender_version}" >&2; return 1; }
    if [[ ! -x "${ASSET_VENV}/bin/python" ]]; then
        python3 -m venv "${ASSET_VENV}"
    fi
    "${ASSET_VENV}/bin/python" -m pip install --disable-pip-version-check -q -r "${SCRIPT_DIR}/tools/asset-conversion-requirements.txt"
    if [[ ! -d "${FAST64_DIR}/.git" ]]; then
        git clone --filter=blob:none https://github.com/Fast-64/fast64.git "${FAST64_DIR}"
    fi
    (cd "${FAST64_DIR}" && git fetch --depth 1 origin "${FAST64_COMMIT}" && git checkout --detach "${FAST64_COMMIT}")
    repo_at_commit "${FAST64_DIR}" "${FAST64_COMMIT}" || { echo "Fast64 checkout is not clean at pinned commit" >&2; return 1; }
}

echo "=== Madeline Cube ROM Toolchain Bootstrap ==="
echo ""
echo "Install target: ${N64_BOOTSTRAP}"
echo "(space-free access symlink; bytes stored in-repo at ${N64_BOOTSTRAP_STORE})"
echo ""

# Quick check: is everything already built?
if [[ -x "${N64_INST}/bin/mips64-elf-gcc" ]] && \
   [[ -f "${N64_INST}/include/n64.mk" ]] && \
   [[ -f "${N64_INST}/include/t3d.mk" ]] && \
   [[ -f "${TINY3D_SRC}/build/libt3d.a" ]] && \
   repo_at_commit "${LIBDRAGON_SRC}" "${LIBDRAGON_COMMIT}" && \
   repo_at_commit "${TINY3D_SRC}" "${TINY3D_COMMIT}" && \
   { [[ -f "${N64_INST}/lib/libdragon.a" ]] || [[ -f "${N64_INST}/mips64-elf/lib/libdragon.a" ]]; }; then
    echo "✓ Toolchain already installed at ${N64_INST}"
    ensure_asset_tools
    echo ""
    echo "You can now run: ./compile-rom.sh"
    exit 0
fi

# ── Step 1: Download GCC toolchain ──────────────────────────────────
TOOLCHAIN_DEB="${N64_BOOTSTRAP}/gcc-toolchain-mips64-x86_64.deb"
if [[ ! -f "${TOOLCHAIN_DEB}" ]]; then
    echo "Step 1: Downloading GCC MIPS64 toolchain..."
    mkdir -p "${N64_BOOTSTRAP}"
    curl -L --fail --retry 3 \
      https://github.com/DragonMinded/libdragon/releases/download/toolchain-continuous-prerelease/gcc-toolchain-mips64-x86_64.deb \
      -o "${TOOLCHAIN_DEB}"
    echo "✓ Downloaded"
else
    echo "Step 1: GCC toolchain deb already present"
fi

# Verify the pinned checksum before extracting (fails closed on a bad download
# or a re-published upstream asset).
actual_deb_sha="$(sha256sum "${TOOLCHAIN_DEB}" | awk '{print $1}')"
if [[ "${actual_deb_sha}" != "${GCC_TOOLCHAIN_SHA256}" ]]; then
    echo "ERROR: GCC toolchain deb checksum mismatch" >&2
    echo "  expected ${GCC_TOOLCHAIN_SHA256}" >&2
    echo "  actual   ${actual_deb_sha}" >&2
    rm -f "${TOOLCHAIN_DEB}"
    exit 1
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
if [[ ( ! -f "${N64_INST}/lib/libdragon.a" && ! -f "${N64_INST}/mips64-elf/lib/libdragon.a" ) ]] || \
   ! repo_at_commit "${LIBDRAGON_SRC}" "${LIBDRAGON_COMMIT}"; then
    echo "Step 3: Cloning & building libdragon (pinned ${LIBDRAGON_COMMIT})..."
    if [[ ! -d "${LIBDRAGON_SRC}/.git" ]]; then
        git clone --filter=blob:none \
          https://github.com/DragonMinded/libdragon.git \
          "${LIBDRAGON_SRC}"
    fi
    (cd "${LIBDRAGON_SRC}" && git fetch --depth 1 origin "${LIBDRAGON_COMMIT}" \
        && git checkout --detach "${LIBDRAGON_COMMIT}")
    repo_at_commit "${LIBDRAGON_SRC}" "${LIBDRAGON_COMMIT}" || { echo "libdragon checkout is not clean at pinned commit" >&2; exit 1; }
    cd "${LIBDRAGON_SRC}"
    # Skip examples: libdragon fetches some from archive.org. Core build errors
    # must propagate under `set -e`.
    run_build_with_logical_dir "${LIBDRAGON_SRC}" "" ./build.sh --no-examples
    echo "✓ libdragon installed"
else
    echo "Step 3: libdragon already built"
fi
if [[ ! -f "${N64_INST}/lib/libdragon.a" && ! -f "${N64_INST}/mips64-elf/lib/libdragon.a" ]]; then
    echo "ERROR: libdragon archive missing after Step 3" >&2
    exit 1
fi

# ── Step 4: Clone & build tiny3d ────────────────────────────────────
if [[ ! -f "${N64_INST}/include/t3d.mk" || ! -f "${TINY3D_SRC}/build/libt3d.a" ]] || \
   ! repo_at_commit "${TINY3D_SRC}" "${TINY3D_COMMIT}"; then
    echo "Step 4: Cloning & building tiny3d (pinned ${TINY3D_COMMIT})..."
    if [[ ! -d "${TINY3D_SRC}/.git" ]]; then
        git clone --filter=blob:none \
          https://github.com/HailToDodongo/tiny3d.git \
          "${TINY3D_SRC}"
    fi
    (cd "${TINY3D_SRC}" && git fetch --depth 1 origin "${TINY3D_COMMIT}" \
        && git checkout --detach "${TINY3D_COMMIT}")
    repo_at_commit "${TINY3D_SRC}" "${TINY3D_COMMIT}" || { echo "tiny3d checkout is not clean at pinned commit" >&2; exit 1; }
    cd "${TINY3D_SRC}"
    run_build_with_logical_dir "${TINY3D_SRC}" "T3D_INST=${TINY3D_SRC} T3D_DIR=${TINY3D_SRC}/" ./build.sh
    echo "✓ tiny3d installed"
else
    echo "Step 4: tiny3d already built"
fi
if [[ ! -f "${N64_INST}/include/t3d.mk" || ! -f "${TINY3D_SRC}/build/libt3d.a" ]]; then
    echo "ERROR: tiny3d install incomplete after Step 4" >&2
    exit 1
fi

ensure_asset_tools

echo ""
echo "=== Bootstrap Complete ==="
echo ""
echo "Toolchain: ${N64_INST}"
echo ""
echo "You can now run: ./compile-rom.sh"
