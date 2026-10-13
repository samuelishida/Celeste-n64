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
    if [ "$name" = "real_map_slope_smoke" ]; then
        sed -n '/^\[real-map-slope\]/p' "$TMP/$name.run.log"
    fi
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
run_cpp player_presentation_smoke tests/player_presentation_smoke.cpp
run_cpp player_pose_cache_smoke tests/player_pose_cache_smoke.cpp
run_cpp profiler_window_smoke tests/profiler_window_smoke.cpp src/user/n64/profiler.cpp -Itests/support/profiler
run_cpp camera_space_math tests/camera_space_math.cpp
run_cpp pass_camera_math tests/pass_camera_math.cpp
run_cpp actor_update_view_smoke tests/actor_update_view_smoke.cpp
run_py actor_update_view_source_smoke tests/actor_update_view_source_smoke.py
run_cpp tile_visibility_contract tests/tile_visibility_contract.cpp
run_cpp lod_math tests/lod_math.cpp
run_cpp fog_math tests/fog_math.cpp
run_cpp skybox_transform tests/skybox_transform.cpp
run_py skybox_geometry_contract tests/skybox_geometry_contract.py

# memory-opt Inc 1: [memory] bucket report shape + budget invariant.
run_cpp memory_budget_contract tests/memory_budget_contract.cpp
run_cpp alignment_audit_contract tests/alignment_audit_contract.cpp
run_cpp renderer_pool_smoke tests/renderer_pool_smoke.cpp
run_cpp render_budgets_contract tests/render_budgets_contract.cpp
run_cpp debug_visualization_contract tests/debug_visualization_contract.cpp
run_cpp debug_flags_contract tests/debug_flags_contract.cpp
run_cpp batch_coalesce_contract tests/batch_coalesce_contract.cpp
run_cpp material_sort_contract tests/material_sort_contract.cpp
run_cpp material_meta_contract_smoke tests/material_meta_contract_smoke.cpp
run_cpp near_visibility_contract tests/near_visibility_contract.cpp
run_cpp renderer_memory_contract tests/renderer_memory_contract.cpp
run_cpp render_counters_contract tests/render_counters_contract.cpp
run_cpp directional_lod_contract tests/directional_lod_contract.cpp
run_cpp input_system_smoke tests/input_system_smoke.cpp
run_cpp input_trace_policy_smoke tests/input_trace_policy_smoke.cpp
# controls-physics-port Inc 1: virtual input device (deadzone + press buffer +
# consume-once) — the layer the InputSystem now feeds.
run_cpp input_compat_smoke tests/input_compat_smoke.cpp
# Streaming & memory opt plan (Inc 1/3/4) — Pattern A, header-only.
run_cpp tile_streamer_diff_smoke tests/tile_streamer_diff_smoke.cpp
run_cpp near_global_sort_smoke tests/near_global_sort_smoke.cpp
run_cpp boot_facing_smoke tests/boot_facing_smoke.cpp
# streaming-experience Inc 1: pending-load state machine (mock is_finished).
run_cpp pending_load_smoke tests/pending_load_smoke.cpp
# streaming-experience: all 13 bounded forward residents stay drawable.
run_cpp drawn_mask_contract tests/drawn_mask_contract.cpp
# streaming-experience Inc 4: reveal state machine + pivot-scale matrix formula.
run_cpp reveal_state_machine tests/reveal_state_machine.cpp

# ── Pattern B (links gameplay sources) ───────────────────────────────
run_cpp mappack_spawn_flags_smoke tests/mappack_spawn_flags_smoke.cpp \
    src/user/gameplay/world/mappack_loader.cpp
run_cpp dynamic_solid_geometry_smoke tests/dynamic_solid_geometry_smoke.cpp \
    src/user/gameplay/world/dynamic_solid_geometry.cpp \
    src/user/gameplay/world/artifact_hash.cpp \
    src/user/gameplay/world/world.cpp \
    src/user/gameplay/physics/coll_mesh.cpp \
    src/user/gameplay/physics/geom.cpp
run_cpp dynamic_solid_batching_smoke tests/dynamic_solid_batching_smoke.cpp
run_cpp actor_world_source_schedule_smoke \
    tests/actor_world_source_schedule_smoke.cpp \
    src/user/gameplay/world/actor_world.cpp
run_cpp cassette_transition_smoke tests/cassette_transition_smoke.cpp \
    src/user/gameplay/actor/cassette_actor.cpp
run_py mappack_v3_test tests/mappack_v3_test.py
run_py mappack_v4_test tests/mappack_v4_test.py
run_py dynamic_solid_artifact_smoke tests/dynamic_solid_artifact_smoke.py
run_py og_hazard_volume_smoke tests/og_hazard_volume_smoke.py
run_cpp hazard_query_smoke tests/hazard_query_smoke.cpp
run_py colmesh_source_face_id_smoke tests/colmesh_source_face_id_smoke.py
run_py og_wall_order_smoke tests/og_wall_order_smoke.py
run_py og_entity_solid_transform_smoke tests/og_entity_solid_transform_smoke.py

# Camera controller tests need the room raycast layer (world.cpp) and the
# physics + runtime math it depends on.
CAMERA_LINK=(
    src/user/gameplay/player/camera_controller.cpp
    src/user/gameplay/world/world.cpp
    src/user/gameplay/physics/coll_mesh.cpp
    src/user/gameplay/physics/geom.cpp
    src/user/gameplay/runtime/math.cpp
)
run_cpp camera_min_distance_smoke tests/camera_min_distance_smoke.cpp "${CAMERA_LINK[@]}"
run_cpp camera_source_target_smoke tests/camera_source_target_smoke.cpp "${CAMERA_LINK[@]}"

# Inc 4 actor type-id query test: needs the actor world + the concrete actor
# .cpp files (their Init/Update/OnCollect definitions).
ACTOR_LINK=(
    src/user/gameplay/world/actor_world.cpp
    src/user/gameplay/actor/strawberry_actor.cpp
    src/user/gameplay/actor/refill_actor.cpp
    src/user/gameplay/actor/spring_actor.cpp
    src/user/gameplay/actor/cassette_actor.cpp
)
run_cpp actor_type_id_smoke tests/actor_type_id_smoke.cpp "${ACTOR_LINK[@]}"
run_cpp actor_owned_pool_smoke tests/actor_owned_pool_smoke.cpp \
    src/user/gameplay/world/actor_world.cpp \
    src/user/gameplay/world/actor_factory.cpp \
    src/user/gameplay/world/entity_dispatch.cpp \
    src/user/gameplay/actor/strawberry_actor.cpp \
    src/user/gameplay/actor/refill_actor.cpp \
    src/user/gameplay/actor/spring_actor.cpp

# Gameplay smoke test (AGENTS.md): grounded jump, air dash, pickup, respawn.
# Needs the full player+world+physics link set (motor sweeps the BVH collision).
GAMEPLAY_LINK=(
    src/user/gameplay/player/player_controller.cpp
    src/user/gameplay/player/camera_controller.cpp
    src/user/gameplay/player/player_movement.cpp
    src/user/gameplay/runtime/math.cpp
    src/user/gameplay/world/world.cpp
    src/user/gameplay/world/collectible.cpp
    src/user/gameplay/world/respawn_system.cpp
    src/user/gameplay/physics/coll_mesh.cpp
    src/user/gameplay/physics/geom.cpp
    "${ACTOR_LINK[@]}"
    src/user/gameplay/world/entity_dispatch.cpp
    src/user/gameplay/world/actor_factory.cpp
    src/user/gameplay/actor/moving_solid_actor.cpp
)
run_cpp gameplay_smoke tests/gameplay_smoke.cpp "${GAMEPLAY_LINK[@]}"

# OG movement-port Inc 2: source-derived grounded analog / grounded-versus-air
# dash vectors, exact walljump launch, and bounded support/wall query fixtures.
run_cpp movement_parity_smoke tests/movement_parity_smoke.cpp "${GAMEPLAY_LINK[@]}"
run_cpp climb_hazard_parity_smoke tests/climb_hazard_parity_smoke.cpp "${GAMEPLAY_LINK[@]}"
run_cpp refill_pickup_parity_smoke tests/refill_pickup_parity_smoke.cpp "${GAMEPLAY_LINK[@]}"
run_cpp og_behavior_trace_smoke tests/og_behavior_trace_smoke.cpp "${GAMEPLAY_LINK[@]}"
run_cpp player_animation_tracks_smoke tests/player_animation_tracks_smoke.cpp
run_cpp animation_pose_math_smoke tests/animation_pose_math_smoke.cpp
run_cpp player_hair_simulation_smoke tests/player_hair_simulation_smoke.cpp
run_cpp player_hair_geometry_smoke tests/player_hair_geometry_smoke.cpp
run_cpp player_hair_color_smoke tests/player_hair_color_smoke.cpp
run_cpp dash_count_parity_smoke tests/dash_count_parity_smoke.cpp src/user/gameplay/player/player_controller.cpp src/user/gameplay/player/player_movement.cpp src/user/gameplay/world/world.cpp src/user/gameplay/physics/coll_mesh.cpp src/user/gameplay/physics/geom.cpp src/user/gameplay/runtime/math.cpp
run_cpp player_head_anchor_smoke tests/player_head_anchor_smoke.cpp
run_cpp player_hair_sphere_smoke tests/player_hair_sphere_smoke.cpp
run_cpp animation_source_curve_smoke tests/animation_source_curve_smoke.cpp
run_cpp player_animation_step_scale_smoke tests/player_animation_step_scale_smoke.cpp
run_py player_animation_source_smoke tests/player_animation_source_smoke.py
run_py animated_model_root_transform_smoke tests/animated_model_root_transform_smoke.py
run_py authored_winding_contract tests/authored_winding_contract.py
run_cpp climb_parity_smoke tests/climb_parity_smoke.cpp "${GAMEPLAY_LINK[@]}"
run_cpp physics_query_regression tests/physics_query_regression.cpp \
    src/user/gameplay/player/player_movement.cpp \
    src/user/gameplay/runtime/math.cpp \
    src/user/gameplay/world/world.cpp \
    src/user/gameplay/world/room_data.cpp \
    src/user/gameplay/physics/coll_mesh.cpp \
    src/user/gameplay/physics/geom.cpp
run_cpp ground_snap_parity_smoke tests/ground_snap_parity_smoke.cpp \
    src/user/gameplay/player/player_movement.cpp \
    src/user/gameplay/runtime/math.cpp \
    src/user/gameplay/world/world.cpp \
    src/user/gameplay/physics/coll_mesh.cpp \
    src/user/gameplay/physics/geom.cpp

# climb-wall-physics Inc 1: wall-rest stability on synthetic collmesh
# (floor + vertical wall). Same link set minus controller-only deps.
run_cpp motor_wall_rest_smoke tests/motor_wall_rest_smoke.cpp \
    src/user/gameplay/player/player_movement.cpp \
    src/user/gameplay/runtime/math.cpp \
    src/user/gameplay/world/world.cpp \
    src/user/gameplay/physics/coll_mesh.cpp \
    src/user/gameplay/physics/geom.cpp

# og-mover-overhaul Inc 1: MoveService single-mover fixture suite
# (anti-tunnel dash, step-down snap, slope ground, waist+head popout,
#  wall-rest equilibrium). Inc 2 wired it into gameplay; Inc 3 relies on it.
MOVE_LINK=(
    src/user/gameplay/player/player_movement.cpp
    src/user/gameplay/player/player_controller.cpp
    src/user/gameplay/runtime/math.cpp
    src/user/gameplay/world/world.cpp
    src/user/gameplay/physics/coll_mesh.cpp
    src/user/gameplay/physics/geom.cpp
)
run_cpp move_service_smoke tests/move_service_smoke.cpp "${MOVE_LINK[@]}"

# og-mover-overhaul Inc 2: migrated motor-behavioral tests onto MoveService +
# the new tick pipeline (respawn contact, scene-update ordering, surface
# parity/slope/ledge, state-phase split). These were dead motor tests; they now
# guard the single-mover behaviors they replaced.
run_cpp respawn_integration_smoke tests/respawn_integration_smoke.cpp "${GAMEPLAY_LINK[@]}"
run_cpp scene_update_order_smoke tests/scene_update_order_smoke.cpp "${GAMEPLAY_LINK[@]}"
run_cpp player_phase_smoke tests/player_phase_smoke.cpp "${GAMEPLAY_LINK[@]}"
run_cpp surface_parity_smoke tests/surface_parity_smoke.cpp \
    src/user/gameplay/player/player_movement.cpp \
    src/user/gameplay/player/player_controller.cpp \
    src/user/gameplay/player/camera_controller.cpp \
    src/user/gameplay/world/room_data.cpp \
    src/user/gameplay/runtime/math.cpp \
    src/user/gameplay/world/world.cpp \
    src/user/gameplay/world/respawn_system.cpp \
    src/user/gameplay/physics/coll_mesh.cpp \
    src/user/gameplay/physics/geom.cpp \
    "${ACTOR_LINK[@]}"

# climb-wall-physics Inc 2/3: motor-integrated climb + mantle behavior.
run_cpp climb_mantle_smoke tests/climb_mantle_smoke.cpp "${GAMEPLAY_LINK[@]}"

# climb-parity-3 Inc 3: OG corner system (inner/slope/outer rays + ease).
run_cpp climb_corner_smoke tests/climb_corner_smoke.cpp "${GAMEPLAY_LINK[@]}"
# climb-animation-alignment Inc 1: reproduces the visual facing gap during
# an outer-corner climb transition. This test is expected to FAIL before Inc 2
# and is flipped to PASS in Inc 2.
run_cpp climb_animation_alignment_smoke tests/climb_animation_alignment_smoke.cpp "${GAMEPLAY_LINK[@]}"

# climb-and-zoom-fix Inc 2: fall speed survives wall seams (face-plane
# impact removal parity with OG Popout).
run_cpp wall_slide_seam_smoke tests/wall_slide_seam_smoke.cpp \
    src/user/gameplay/player/player_movement.cpp \
    src/user/gameplay/runtime/math.cpp \
    src/user/gameplay/world/world.cpp \
    src/user/gameplay/physics/coll_mesh.cpp \
    src/user/gameplay/physics/geom.cpp

# og-mover-overhaul Inc 3: climb onto the single mover — corner adopt + fast
# dash must never leave residual penetration or tunnel through the wall.
run_cpp climb_no_clip_smoke tests/climb_no_clip_smoke.cpp "${GAMEPLAY_LINK[@]}"

# climb-bug probe (autoresearch): sloped/overhang/thin-wall climb regression
# (wallUp movement, waist-only climb popout, entry snap without the 3-unit
# cap, corner lateral gate + pre-normalization projection check).
run_cpp climb_bug_probe tests/climb_bug_probe.cpp "${GAMEPLAY_LINK[@]}"

# og-mover-overhaul Inc 4: Skidding state + dash steering + dash input buffer.
run_cpp skid_smoke tests/skid_smoke.cpp "${GAMEPLAY_LINK[@]}"
run_cpp dash_jump_smoke tests/feel_spec/dash_jump_smoke.cpp "${GAMEPLAY_LINK[@]}"
run_cpp dash_smoke tests/feel_spec/dash_smoke.cpp "${GAMEPLAY_LINK[@]}"
run_cpp coyote_edge_smoke tests/feel_spec/coyote_edge_smoke.cpp "${GAMEPLAY_LINK[@]}"
run_cpp locomotion_smoke tests/feel_spec/locomotion_smoke.cpp "${GAMEPLAY_LINK[@]}"
run_cpp slope_smoke tests/feel_spec/slope_smoke.cpp "${GAMEPLAY_LINK[@]}"

# review C5: previously unwired contracts + feel_spec tests — now in the
# runner so their constants assertions are enforced on every suite run.
run_cpp movement_contracts_test tests/movement_contracts_test.cpp "${GAMEPLAY_LINK[@]}"
run_cpp physics_contracts_test tests/physics_contracts_test.cpp \
    src/user/gameplay/runtime/math.cpp
run_cpp oneway_smoke tests/feel_spec/oneway_smoke.cpp \
    src/user/gameplay/player/player_movement.cpp \
    src/user/gameplay/runtime/math.cpp \
    src/user/gameplay/world/world.cpp \
    src/user/gameplay/physics/coll_mesh.cpp \
    src/user/gameplay/physics/geom.cpp
run_cpp ledge_smoke tests/feel_spec/ledge_smoke.cpp "${GAMEPLAY_LINK[@]}" \
    src/user/gameplay/world/room_data.cpp

# wall-climb-overhaul Inc 1: plane-based wall query returns stable face
# normals (no seam diagonal) and rejects slopes (OG ClimbNormalCheck).
run_cpp wall_plane_query_smoke tests/wall_plane_query_smoke.cpp \
    src/user/gameplay/runtime/math.cpp \
    src/user/gameplay/world/world.cpp \
    src/user/gameplay/physics/coll_mesh.cpp \
    src/user/gameplay/physics/geom.cpp

# ── Pattern C (links mappack_loader.cpp) ─────────────────────────────
# These need a baked map-pack fixture. Use the repo's baked staging dir if
# present; otherwise skip with a warning (they can be run after `make bake`).
STAGING="build/bake-fc-1200/staging"
if [ -f "$STAGING/forsyken-city.mappack" ]; then
    run_py interconnected_colmesh_smoke tests/colmesh_smoke.py \
        "$STAGING/forsyken-city.colmesh"
    run_cpp interconnected_collision_smoke tests/interconnected_collision_smoke.cpp \
        src/user/gameplay/physics/coll_mesh.cpp \
        src/user/gameplay/physics/geom.cpp -- "$STAGING/forsyken-city.colmesh"
    run_cpp map_runtime_v2_smoke tests/map_runtime_v2_smoke.cpp \
        src/user/gameplay/world/map_runtime.cpp \
        src/user/gameplay/world/mappack_loader.cpp \
        src/user/gameplay/world/dynamic_solid_geometry.cpp \
        src/user/gameplay/world/artifact_hash.cpp \
        src/user/gameplay/world/level_loader.cpp \
        src/user/gameplay/world/actor_world.cpp \
        src/user/gameplay/world/actor_factory.cpp \
        src/user/gameplay/world/entity_dispatch.cpp \
        src/user/gameplay/actor/strawberry_actor.cpp \
        src/user/gameplay/actor/refill_actor.cpp \
        src/user/gameplay/actor/spring_actor.cpp \
        src/user/gameplay/world/world.cpp \
        src/user/gameplay/physics/coll_mesh.cpp \
        src/user/gameplay/physics/geom.cpp -- \
        "$STAGING/forsyken-city.mappack" "$STAGING"
    run_cpp dynamic_solid_query_smoke tests/dynamic_solid_query_smoke.cpp \
        src/user/gameplay/world/map_runtime.cpp \
        src/user/gameplay/world/mappack_loader.cpp \
        src/user/gameplay/world/dynamic_solid_geometry.cpp \
        src/user/gameplay/world/artifact_hash.cpp \
        src/user/gameplay/world/level_loader.cpp \
        src/user/gameplay/world/world.cpp \
        src/user/gameplay/physics/coll_mesh.cpp \
        src/user/gameplay/physics/geom.cpp -- \
        "$STAGING/forsyken-city.mappack" "$STAGING"
    run_cpp dynamic_solid_render_smoke tests/dynamic_solid_render_smoke.cpp \
        src/user/gameplay/world/map_runtime.cpp \
        src/user/gameplay/world/mappack_loader.cpp \
        src/user/gameplay/world/dynamic_solid_geometry.cpp \
        src/user/gameplay/world/artifact_hash.cpp \
        src/user/gameplay/world/level_loader.cpp \
        src/user/gameplay/world/world.cpp \
        src/user/gameplay/physics/coll_mesh.cpp \
    src/user/gameplay/physics/geom.cpp -- \
        "$STAGING/forsyken-city.mappack" "$STAGING"
    run_py dynamic_solid_matrix_lifetime_contract \
        tests/dynamic_solid_matrix_lifetime_contract.py
    run_cpp dynamic_solid_rider_parity_smoke \
        tests/dynamic_solid_rider_parity_smoke.cpp \
        src/user/gameplay/player/player_controller.cpp \
        src/user/gameplay/player/player_movement.cpp \
        src/user/gameplay/runtime/math.cpp \
        src/user/gameplay/world/dynamic_solid_world.cpp \
        src/user/gameplay/world/map_runtime.cpp \
        src/user/gameplay/world/mappack_loader.cpp \
        src/user/gameplay/world/dynamic_solid_geometry.cpp \
        src/user/gameplay/world/artifact_hash.cpp \
        src/user/gameplay/world/level_loader.cpp \
        src/user/gameplay/world/actor_world.cpp \
        src/user/gameplay/world/actor_factory.cpp \
        src/user/gameplay/world/entity_dispatch.cpp \
        src/user/gameplay/actor/strawberry_actor.cpp \
        src/user/gameplay/actor/refill_actor.cpp \
        src/user/gameplay/actor/spring_actor.cpp \
        src/user/gameplay/world/world.cpp \
        src/user/gameplay/physics/coll_mesh.cpp \
        src/user/gameplay/physics/geom.cpp -- \
        "$STAGING/forsyken-city.mappack" "$STAGING"
    run_cpp tile_streamer_smoke tests/tile_streamer_smoke.cpp \
        src/user/gameplay/world/mappack_loader.cpp -- "$STAGING/forsyken-city.mappack"
    run_cpp tile_stream_lru_contract tests/tile_stream_lru_contract.cpp \
        src/user/gameplay/world/mappack_loader.cpp
    run_cpp material_catalog_test tests/material_catalog_test.cpp \
        src/user/gameplay/world/mappack_loader.cpp -- "$STAGING"
    # streaming-experience Inc 2: per-cell resident cost table + MaxLvlSize check.
    run_cpp streaming_memory_measure tests/streaming_memory_measure.cpp \
        src/user/gameplay/world/mappack_loader.cpp -- "$STAGING" "$STAGING/forsyken-city.mappack"
    # streaming-experience Inc 3: autowalk distance guarantees a seam crossing.
    run_cpp autowalk_distance_contract tests/autowalk_distance_contract.cpp \
        src/user/gameplay/world/mappack_loader.cpp -- "$STAGING/forsyken-city.mappack"
    # memory-opt Inc 2: load-path plumbing (flag-off/fopen path parses staging).
    run_cpp mappack_compression_contract tests/mappack_compression_contract.cpp \
        src/user/gameplay/world/mappack_loader.cpp -- "$STAGING/forsyken-city.mappack"
    # Current movement stack against real geometry: no-teleport Start-to-wall
    # approach, climb, mantle, and per-tick waist/head collision clearance.
    run_cpp real_map_guided_climb_smoke tests/real_map_guided_climb_smoke.cpp \
        src/user/gameplay/player/player_movement.cpp \
        src/user/gameplay/player/player_controller.cpp \
        src/user/gameplay/runtime/math.cpp \
        src/user/gameplay/world/world.cpp \
        src/user/gameplay/world/mappack_loader.cpp \
        src/user/gameplay/physics/coll_mesh.cpp \
        src/user/gameplay/physics/geom.cpp -- \
        "$STAGING/forsyken-city.mappack" "$STAGING/forsyken-city.colmesh"
    # Wall-jump from the real climb entry checks the shared launch helper and
    # confirms that held climb input cannot select or suppress the launch.
    run_cpp real_map_walljump_smoke tests/real_map_walljump_smoke.cpp \
        src/user/gameplay/player/player_movement.cpp \
        src/user/gameplay/player/player_controller.cpp \
        src/user/gameplay/runtime/math.cpp \
        src/user/gameplay/world/world.cpp \
        src/user/gameplay/world/mappack_loader.cpp \
        src/user/gameplay/physics/coll_mesh.cpp \
        src/user/gameplay/physics/geom.cpp -- \
        "$STAGING/forsyken-city.mappack" "$STAGING/forsyken-city.colmesh"
    # Current slope-speed implementation on stable walkable geometry selected
    # from the baked map, in both travel directions and against flat baseline.
    run_cpp real_map_slope_smoke tests/real_map_slope_smoke.cpp \
        src/user/gameplay/player/player_movement.cpp \
        src/user/gameplay/player/player_controller.cpp \
        src/user/gameplay/runtime/math.cpp \
        src/user/gameplay/world/world.cpp \
        src/user/gameplay/world/mappack_loader.cpp \
        src/user/gameplay/physics/coll_mesh.cpp \
        src/user/gameplay/physics/geom.cpp -- \
        "$STAGING/forsyken-city.mappack" "$STAGING/forsyken-city.colmesh"
    # Finite-edge corner route: reject a back-side anchor; keep each climb step
    # bounded and release cleanly when adjoining front-side wall is unreachable.
    run_cpp real_map_inner_corner_clearance_smoke \
        tests/real_map_inner_corner_clearance_smoke.cpp \
        src/user/gameplay/player/player_movement.cpp \
        src/user/gameplay/player/player_controller.cpp \
        src/user/gameplay/runtime/math.cpp \
        src/user/gameplay/world/world.cpp \
        src/user/gameplay/world/mappack_loader.cpp \
        src/user/gameplay/physics/coll_mesh.cpp \
        src/user/gameplay/physics/geom.cpp -- \
        "$STAGING/forsyken-city.mappack" "$STAGING/forsyken-city.colmesh"
    # Real-map corner traversal: approach on the old wall, ease onto the
    # adopted wall, and gate anchor continuity, grip retention and model gap.
    run_cpp real_map_corner_traverse_smoke \
        tests/real_map_corner_traverse_smoke.cpp \
        src/user/gameplay/player/player_movement.cpp \
        src/user/gameplay/player/player_controller.cpp \
        src/user/gameplay/runtime/math.cpp \
        src/user/gameplay/world/world.cpp \
        src/user/gameplay/world/mappack_loader.cpp \
        src/user/gameplay/physics/coll_mesh.cpp \
        src/user/gameplay/physics/geom.cpp -- \
        "$STAGING/forsyken-city.mappack" "$STAGING/forsyken-city.colmesh"
else
    echo "SKIP: Pattern C tests (no baked fixture at $STAGING; run 'make bake-forsaken-city')"
fi

# ── Python contracts ─────────────────────────────────────────────────
run_py asset_manifest_smoke tests/asset_manifest_smoke.py
run_py texture_profile_smoke tests/texture_profile_smoke.py
run_py texture_uv_rebase_smoke tests/texture_uv_rebase_smoke.py
run_py mipmap_upload_contract tests/mipmap_upload_contract.py
run_py material_budget_smoke tests/material_budget_smoke.py
run_py static_model_asset_smoke tests/static_model_asset_smoke.py
run_py refill_render_contract_smoke tests/refill_render_contract_smoke.py
run_py player_color_bake_smoke tests/player_color_bake_smoke.py
run_py static_player_prepare_smoke tests/static_player_prepare_smoke.py
run_py player_texture_packaging_smoke tests/player_texture_packaging_smoke.py
run_py animated_model_asset_smoke tests/animated_model_asset_smoke.py
run_py animated_player_t3dm_smoke tests/static_player_t3dm_smoke.py
run_py player_animation_curve_sidecar tests/player_animation_curve_sidecar_smoke.py
run_py weighted_skin_asset_smoke tests/weighted_skin_asset_smoke.py
run_py animated_joint_pose_parity_smoke tests/animated_joint_pose_parity_smoke.py
run_cpp weighted_skinning_math_smoke tests/weighted_skinning_math_smoke.cpp
run_cpp player_model_root_offset_smoke tests/player_model_root_offset_smoke.cpp
run_cpp animated_player_runtime_contract tests/static_player_runtime_contract.cpp
# climb-animation-alignment Inc 3: animation selection priority is independent
# of visual-facing state.
run_cpp animation_state_priority_smoke tests/animation_state_priority_smoke.cpp
run_cpp animation_interrupt_parity_smoke tests/animation_interrupt_parity_smoke.cpp
run_cpp player_animation_facing_smoke tests/player_animation_facing_smoke.cpp "${GAMEPLAY_LINK[@]}"
run_py audio_manifest_smoke tests/audio_manifest_smoke.py
run_py interconnected_map_contract tests/interconnected_map_contract.py
run_py interconnected_seam_equivalence tests/interconnected_seam_equivalence.py
run_py render_pipeline_contract tests/render_pipeline_contract.py
run_py z_split_runtime_contract tests/z_split_runtime_contract.py
run_py distant_lod_contract tests/distant_lod_contract.py
run_py distant_decimation_contract tests/distant_decimation_contract.py
run_py dlod_format_contract tests/dlod_format_contract.py
run_py physics_capture_verifier_smoke tests/physics_capture_verifier_smoke.py

echo
echo "=== $PASSES passed, $FAILURES failed ==="
[ "$FAILURES" -eq 0 ]
