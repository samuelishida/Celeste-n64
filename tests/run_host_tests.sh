#!/usr/bin/env bash
# Host test runner (Inc 7). Builds + runs every host C++ smoke test and runs
# every Python contract test with one command. Exit nonzero on any failure.
#
# All host tests are toolchain-independent (no N64 toolchain needed).
#
# Usage:
#   ./tests/run_host_tests.sh
set -u

REPO="$(cd "$(dirname "$0")/.." && pwd)"
cd "$REPO"

# Writable temp dir for build outputs (the sandbox /tmp may be read-only).
TMP="${TMPDIR:-/tmp}/hawk-host-tests"
mkdir -p "$TMP"

FAILURES=0
PASSES=0

run_cpp() {
    # run_cpp <name> <source> [extra sources...] -- [run args...]
    local name="$1"; shift
    local src="$1"; shift
    local bin="$TMP/$name"
    local build_srcs=()
    local run_args=()
    # Split "--" to separate build sources from run args.
    while [ "$#" -gt 0 ]; do
        if [ "$1" = "--" ]; then shift; run_args=("$@"); break; fi
        build_srcs+=("$1"); shift
    done
    if ! g++ -std=c++17 -Isrc/user "$src" "${build_srcs[@]}" -o "$bin" 2>"$TMP/$name.build.log"; then
        echo "FAIL(build): $name"
        sed -n '1,20p' "$TMP/$name.build.log"
        FAILURES=$((FAILURES+1))
        return 1
    fi
    if ! "$bin" "${run_args[@]}" >"$TMP/$name.run.log" 2>&1; then
        echo "FAIL(run): $name"
        sed -n '1,20p' "$TMP/$name.run.log"
        FAILURES=$((FAILURES+1))
        return 1
    fi
    echo "PASS: $name"
    PASSES=$((PASSES+1))
}

run_py() {
    # run_py <name> <script>
    local name="$1"; shift
    if ! python3 "$@" >"$TMP/$name.py.log" 2>&1; then
        echo "FAIL(py): $name"
        sed -n '1,20p' "$TMP/$name.py.log"
        FAILURES=$((FAILURES+1))
        return 1
    fi
    echo "PASS: $name"
    PASSES=$((PASSES+1))
}

# ── Pattern A (header-only, no N64 deps) ─────────────────────────────
run_cpp camera_space_math tests/camera_space_math.cpp
run_cpp pass_camera_math tests/pass_camera_math.cpp
run_cpp tile_visibility_contract tests/tile_visibility_contract.cpp
run_cpp frame_order_contract tests/frame_order_contract.cpp
run_cpp lod_math tests/lod_math.cpp
run_cpp distant_pass_order tests/distant_pass_order.cpp
run_cpp fog_math tests/fog_math.cpp
run_cpp skybox_transform tests/skybox_transform.cpp
run_cpp render_budgets_contract tests/render_budgets_contract.cpp
run_cpp debug_visualization_contract tests/debug_visualization_contract.cpp
run_cpp debug_flags_contract tests/debug_flags_contract.cpp
run_cpp distant_cull_contract tests/distant_cull_contract.cpp
run_cpp distant_overlap_contract tests/distant_overlap_contract.cpp
run_cpp distant_streaming_contract tests/distant_streaming_contract.cpp
run_cpp distant_distance_contract tests/distant_distance_contract.cpp
run_cpp batch_coalesce_contract tests/batch_coalesce_contract.cpp
run_cpp material_sort_contract tests/material_sort_contract.cpp
run_cpp distant_sort_contract tests/distant_sort_contract.cpp
run_cpp near_visibility_contract tests/near_visibility_contract.cpp
run_cpp renderer_memory_contract tests/renderer_memory_contract.cpp
run_cpp render_counters_contract tests/render_counters_contract.cpp
run_cpp distant_cellstats_contract tests/distant_cellstats_contract.cpp
run_cpp distant_dedup_contract tests/distant_dedup_contract.cpp
run_cpp distant_shared_matrix_contract tests/distant_shared_matrix_contract.cpp
run_cpp dlod_format_contract tests/dlod_format_contract.cpp
run_cpp directional_lod_contract tests/directional_lod_contract.cpp
run_cpp input_system_smoke tests/input_system_smoke.cpp
# Streaming & memory opt plan (Inc 1/3/4) — Pattern A, header-only.
# (Inc 2 was skipped as infeasible, so there is no distant_shared_verts_smoke.)
run_cpp tile_streamer_diff_smoke tests/tile_streamer_diff_smoke.cpp
run_cpp distant_no_block_smoke tests/distant_no_block_smoke.cpp
run_cpp near_global_sort_smoke tests/near_global_sort_smoke.cpp

# ── Pattern C (links mappack_loader.cpp) ─────────────────────────────
# These need a baked map-pack fixture. Use the repo's baked staging dir if
# present; otherwise skip with a warning (they can be run after `make bake`).
STAGING="build/bake-fc-1200/staging"
if [ -f "$STAGING/forsyken-city.mappack" ]; then
    run_cpp tile_streamer_smoke tests/tile_streamer_smoke.cpp \
        src/user/gameplay/world/mappack_loader.cpp -- "$STAGING/forsyken-city.mappack"
    run_cpp tile_stream_lru_contract tests/tile_stream_lru_contract.cpp \
        src/user/gameplay/world/mappack_loader.cpp
    run_cpp material_catalog_test tests/material_catalog_test.cpp \
        src/user/gameplay/world/mappack_loader.cpp -- "$STAGING"
else
    echo "SKIP: Pattern C tests (no baked fixture at $STAGING; run 'make bake-forsaken-city')"
fi

# ── Python contracts ─────────────────────────────────────────────────
run_py interconnected_map_contract tests/interconnected_map_contract.py
run_py interconnected_seam_equivalence tests/interconnected_seam_equivalence.py
run_py render_pipeline_contract tests/render_pipeline_contract.py
run_py distant_lod_contract tests/distant_lod_contract.py
run_py distant_decimation_contract tests/distant_decimation_contract.py
run_py dlod_format_contract tests/dlod_format_contract.py

echo
echo "=== $PASSES passed, $FAILURES failed ==="
[ "$FAILURES" -eq 0 ]
