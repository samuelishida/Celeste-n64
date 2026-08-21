// Host test for boot-facing orientation (the z-split positional cull bug).
//
// Bug: the player boots with default facing +Z (player_state.hpp) and the
// camera cold-start forward is +Z (camera_controller.cpp). The Forsaken City
// start spawn is at the +Z edge (iz=0); the map extends in -Z (iz=-7..0).
// With +Z facing, the camera looks AWAY from the map → the distant frustum
// cone cull (CellAabbInDistantFrustum) rejects every cell behind the camera
// (the entire map) → "map disappears in half" until the player turns around.
//
// Fix: orient the player + camera toward the map center at boot. This test
// verifies the math: given a +Z-edge spawn and a -Z map center, the facing
// points -Z and the distant cone cull passes cells ahead (in -Z).
//
// Build:
//   g++ -std=c++17 -Isrc/user tests/boot_facing_smoke.cpp
#include <cstdio>
#include <cmath>

#include "gameplay/math_types.hpp"
#include "gameplay/render/lod_math.hpp"

using namespace madeline_cube;

static int failures = 0;

static void expect(bool cond, const char* what) {
    if (!cond) {
        std::fprintf(stderr, "FAIL: %s\n", what);
        ++failures;
    }
}

int main() {
    // Forsaken City layout (from the baked mappack):
    //   world XZ: x=[-637, 1632], z=[-1677, 250]
    //   start spawn: cell_00_00 at ~(120, 58, 120) — the +Z edge
    //   map center XZ: (497.5, -713.6)
    const Vec3 spawn = {120.0f, 58.0f, 120.0f};
    const AABB world_bounds = {{-637.0f, -165.0f, -1677.0f},
                               {1632.0f, 544.0f, 250.0f}};
    const Vec3 map_center = {
        (world_bounds.min.x + world_bounds.max.x) * 0.5f,
        0.0f,
        (world_bounds.min.z + world_bounds.max.z) * 0.5f,
    };

    // Compute the boot facing toward the map center (same math as
    // gameplay_scene.cpp boot-facing fix).
    Vec3 to_center = {
        map_center.x - spawn.x,
        0.0f,
        map_center.z - spawn.z,
    };
    const float len2 = to_center.x * to_center.x + to_center.z * to_center.z;
    expect(len2 > 1e-4f, "map center is not on top of spawn");
    const float inv_len = 1.0f / std::sqrt(len2);
    to_center.x *= inv_len;
    to_center.z *= inv_len;

    // The facing must point into the map (negative Z), not the default +Z.
    expect(to_center.z < -0.5f, "boot facing points into -Z (toward map)");
    expect(to_center.z != 1.0f, "boot facing is NOT the default +Z");

    // The camera looks from behind the player toward the player (i.e. in the
    // facing direction). Simulate: camera = spawn - facing * 60, target = spawn.
    const float cam_dist = 60.0f;
    const Vec3 cam_pos = {
        spawn.x - to_center.x * cam_dist,
        spawn.y + 30.0f,
        spawn.z - to_center.z * cam_dist,
    };
    const Vec3 cam_target = {spawn.x, spawn.y + 12.0f, spawn.z};

    // Frustum cull params used to verify boot-facing orientation. The
    // `CellAabbInDistantFrustum` cone predicate in lod_math.hpp is the shared
    // AABB-cone math; after the distant pass was removed it still gates the
    // boot-facing fix that keeps the map on-screen at spawn.
    const float cell_w = 240.0f;  // chunk_size(1200) * scale(0.2)
    const float vfov = 45.0f;
    const float vfov_rad = vfov * 0.5f * (float)kLodPi / 180.0f;
    const float hfov_deg = 2.0f * std::atan(std::tan(vfov_rad) * (4.0f / 3.0f)) *
                           (180.0f / (float)kLodPi);
    const float near_d = 0.25f * cell_w;  // ~60 (small: fills ring cells the near cone misses)
    const float far_d = 3423.0f;

    // A cell deep in the map (iz=-5, x≈120, z≈-1080) should pass the cone cull.
    AABB deep_cell = {{0.0f, 0.0f, -1200.0f}, {240.0f, 200.0f, -960.0f}};
    bool deep_visible = CellAabbInDistantFrustum(
        cam_pos, cam_target, hfov_deg, near_d, far_d, deep_cell, kCullMargin);
    expect(deep_visible, "deep cell (iz=-5) is visible with boot-facing fix");

    // A cell behind the camera (iz=0, +Z side, past the camera) should be
    // culled. The camera is pulled ~60u behind the spawn on the +Z side, so
    // a cell starting at z=360 is fully behind it. With the old 508u near
    // plane this assertion was masked by a depth cull; with the fixed 60u
    // near plane it relies on the angular cone cull (real regression guard).
    AABB behind_cell = {{0.0f, 0.0f, 360.0f}, {240.0f, 200.0f, 600.0f}};
    bool behind_visible = CellAabbInDistantFrustum(
        cam_pos, cam_target, hfov_deg, near_d, far_d, behind_cell, kCullMargin);
    expect(!behind_visible, "cell behind camera (+Z) is culled");

    // Now verify the BUG: with the OLD default +Z facing, the deep cell is
    // NOT visible (the camera looks away from the map).
    const Vec3 old_facing = {0.0f, 0.0f, 1.0f};  // default
    const Vec3 old_cam_pos = {
        spawn.x - old_facing.x * cam_dist,
        spawn.y + 30.0f,
        spawn.z - old_facing.z * cam_dist,
    };
    // old_cam_pos is at z=60, looking +Z toward spawn at z=120
    const Vec3 old_cam_target = {spawn.x, spawn.y + 12.0f, spawn.z};
    bool deep_visible_old = CellAabbInDistantFrustum(
        old_cam_pos, old_cam_target, hfov_deg, near_d, far_d, deep_cell,
        kCullMargin);
    expect(!deep_visible_old,
           "BUG REPRO: deep cell invisible with default +Z facing");

    if (failures == 0) {
        std::printf("boot_facing_smoke: all passed\n");
        return 0;
    }
    std::printf("boot_facing_smoke: %d FAILED\n", failures);
    return 1;
}