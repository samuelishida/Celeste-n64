#pragma once

#include <algorithm>
#include <cmath>

#include "gameplay/math_types.hpp"
#include "gameplay/world/mappack_loader.hpp"  // AABB, V2RoomSpec

namespace madeline_cube {

// LOD threshold constant (arch.md §10). The squared-distance threshold below
// which a higher-detail child representation is retained.
inline constexpr float kLevel2MinDistance = 500.0f;

// Pi for degree->radian conversions (no global constant in math_types.hpp).
inline constexpr float kLodPi = 3.14159265358979f;

// Union of room AABBs (world XZ extent). Returns an empty AABB if count==0.
inline AABB UnionRoomsAABB(const V2RoomSpec* rooms, int count) {
    AABB u = {{0.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 0.0f}};
    if (!rooms || count <= 0) return u;
    u = rooms[0].world_aabb;
    for (int i = 1; i < count; ++i) {
        u.min.x = std::min(u.min.x, rooms[i].world_aabb.min.x);
        u.min.z = std::min(u.min.z, rooms[i].world_aabb.min.z);
        u.max.x = std::max(u.max.x, rooms[i].world_aabb.max.x);
        u.max.z = std::max(u.max.z, rooms[i].world_aabb.max.z);
    }
    return u;
}

// Frustum-cull cone margin (Inc 4 / D3). >1 widens the cone so horizon cells
// don't pop at the exact screen edge. Shared by the renderer and the
// host-safe AABB cull helper below (single source of truth). Tuned on device.
inline constexpr float kCullMargin = 1.15f;

// Cull a distant cell only when its WHOLE XZ AABB is outside the camera cone
// + depth range (Inc 4 / D3). Per-corner angular test widened by
// `atan(half_diag / dist)` so the cone cannot pass through the cell interior
// with all 4 corners outside (the exact screen-edge pop-in bug class); near/far
// depth slack scaled by the cell half-extent. Keeps the cell if ANY corner
// passes. Host-safe — pure Vec3/AABB/float math.
inline bool CellAabbInDistantFrustum(const Vec3& cam_pos, const Vec3& cam_target,
                                     float hfov_deg, float near_d, float far_d,
                                     const AABB& aabb,
                                     float margin = kCullMargin,
                                     float extent_slack = 1.0f) {
    if (near_d >= far_d) return false;          // empty frustum
    if (far_d <= 0.0f) return false;
    if (margin < 1.0f) margin = 1.0f;

    // Horizontal facing direction (camera -> target, XZ plane).
    const float fx = cam_target.x - cam_pos.x;
    const float fz = cam_target.z - cam_pos.z;
    const float flen2 = fx * fx + fz * fz;
    if (flen2 < 1e-6f) return false;            // degenerate facing — cull
    const float inv_flen = 1.0f / std::sqrt(flen2);
    const float fdx = fx * inv_flen;
    const float fdz = fz * inv_flen;

    // Cell XZ half-extent + half-diagonal (≈170u for a 240u cell).
    const float hx = (aabb.max.x - aabb.min.x) * 0.5f;
    const float hz = (aabb.max.z - aabb.min.z) * 0.5f;
    const float half_diag = std::sqrt(hx * hx + hz * hz);

    const float half_rad = (hfov_deg * 0.5f) * (kLodPi / 180.0f) * margin;

    // Test each of the 4 XZ corners. A corner passes if it is within the
    // (margined + extent-widened) cone AND within the depth range (with
    // extent slack). Keep the cell if ANY corner passes.
    const float corners[4][2] = {
        {aabb.min.x, aabb.min.z}, {aabb.max.x, aabb.min.z},
        {aabb.min.x, aabb.max.z}, {aabb.max.x, aabb.max.z},
    };
    for (int c = 0; c < 4; ++c) {
        const float dx = corners[c][0] - cam_pos.x;
        const float dz = corners[c][1] - cam_pos.z;
        const float dist = std::sqrt(dx * dx + dz * dz);
        // Depth range with extent slack.
        if (dist < near_d - extent_slack * half_diag) continue;
        if (dist > far_d + extent_slack * half_diag) continue;
        // Angular test widened by atan(half_diag / dist) so the cone cannot
        // pass through the cell interior with all 4 corners outside.
        const float inv_dist = dist > 1e-6f ? 1.0f / dist : 0.0f;
        const float dot = (dx * fdx + dz * fdz) * inv_dist;
        const float widen = (dist > 1e-6f) ? std::atan(half_diag / dist) : 0.0f;
        const float cos_widened = std::cos(half_rad + widen);
        if (dot >= cos_widened) return true;
    }
    return false;
}

// A resident cell is drawn by the near pass iff its AABB intersects the camera
// cone (near FOV/depth) — no grid-index cut, no radial extent beyond residency
// (the ring IS the coverage). Host-safe.
//
// `fov_deg` is the VERTICAL FOV (as passed to t3d_viewport_set_projection);
// the cone is widened to the horizontal FOV (4:3 aspect) and then by
// `kCullMargin` plus a per-corner `atan(half_diag / dist)` slack, so the cone
// does not pass through a resident cell interior with all 4 corners outside.
// Depth 5..800 is effectively a no-op for the ring (all ring cells < 800u) —
// the cone is the only effective cull.
inline bool CellAabbInNearCone(const Vec3& cam_pos, const Vec3& cam_target,
                               float fov_deg, float near_d, float far_d,
                               const AABB& aabb) {
    if (near_d >= far_d) return false;
    if (far_d <= 0.0f) return false;

    // Horizontal facing direction (camera -> target, XZ plane).
    const float fx = cam_target.x - cam_pos.x;
    const float fz = cam_target.z - cam_pos.z;
    const float flen2 = fx * fx + fz * fz;
    if (flen2 < 1e-6f) return true;  // degenerate facing — draw (safe)
    const float inv_flen = 1.0f / std::sqrt(flen2);
    const float fdx = fx * inv_flen;
    const float fdz = fz * inv_flen;

    // Horizontal full FOV from the vertical FOV + 4:3 aspect, then widened by
    // kCullMargin so on-screen cells near the edge don't pop.
    const float vfov_rad = fov_deg * 0.5f * (kLodPi / 180.0f);
    const float hfov_deg = 2.0f * std::atan(std::tan(vfov_rad) * (4.0f / 3.0f)) *
                           (180.0f / kLodPi);
    const float half_rad = (hfov_deg * 0.5f) * (kLodPi / 180.0f) * kCullMargin;

    // Cell XZ half-extent + half-diagonal (≈170u for a 240u cell).
    const float hx = (aabb.max.x - aabb.min.x) * 0.5f;
    const float hz = (aabb.max.z - aabb.min.z) * 0.5f;
    const float half_diag = std::sqrt(hx * hx + hz * hz);

    // Test each of the 4 XZ corners. A corner passes if it is within the
    // (margined + extent-widened) cone AND within the depth range. Keep the
    // cell if ANY corner passes — same widening rule as
    // CellAabbInDistantFrustum, so the two predicates cannot disagree on
    // angular coverage.
    const float corners[4][2] = {
        {aabb.min.x, aabb.min.z}, {aabb.max.x, aabb.min.z},
        {aabb.min.x, aabb.max.z}, {aabb.max.x, aabb.max.z},
    };
    for (int c = 0; c < 4; ++c) {
        const float dx = corners[c][0] - cam_pos.x;
        const float dz = corners[c][1] - cam_pos.z;
        const float dist = std::sqrt(dx * dx + dz * dz);
        if (dist < near_d || dist > far_d) continue;
        const float inv_dist = dist > 1e-6f ? 1.0f / dist : 0.0f;
        const float dot = (dx * fdx + dz * fdz) * inv_dist;
        const float widen = (dist > 1e-6f) ? std::atan(half_diag / dist) : 0.0f;
        const float cos_widened = std::cos(half_rad + widen);
        if (dot >= cos_widened) return true;
    }
    return false;
}

// Per-direction mesh index (arch.md §11-12). A distant LOD entry can hold up
// to 4 directional meshes (N/S/E/W). This returns the index for a given
// relative direction vector.
//
// The four directions are indexed:
//   0 = +Z (south, toward camera when looking -Z)
//   1 = -Z (north)
//   2 = +X (east)
//   3 = -X (west)
inline int DirectionalIndexFromDelta(const Vec3& delta) {
    const float adx = std::fabs(delta.x);
    const float adz = std::fabs(delta.z);
    if (adx >= adz) {
        return delta.x >= 0.0f ? 2 : 3;  // east / west
    }
    return delta.z >= 0.0f ? 0 : 1;      // south / north
}

// Select the directional mesh index for a tile given the camera position and
// the camera's facing direction (arch.md §12). When the camera is very close
// to the tile center (|delta.x| and |delta.z| both < `close_threshold`), use
// the camera's own direction to avoid unstable directional selection around
// the center of the world; otherwise use the tile->camera delta direction.
inline int DirectionalMeshIndex(const Vec3& camera_pos, const Vec3& tile_origin,
                                const Vec3& camera_dir, float close_threshold) {
    const Vec3 delta = {camera_pos.x - tile_origin.x,
                        camera_pos.y - tile_origin.y,
                        camera_pos.z - tile_origin.z};
    if (std::fabs(delta.x) < close_threshold &&
        std::fabs(delta.z) < close_threshold) {
        return DirectionalIndexFromDelta(camera_dir);
    }
    return DirectionalIndexFromDelta(delta);
}

// Select the LOD level for a distant tile (arch.md §10). Returns the LOD
// level (0 = highest detail, closer) whose squared-distance threshold
// `dist² < kLevel2MinDistance² * lod_scale²` is satisfied. `max_level` bounds
// the returned level (the furthest/baked representation).
//
// The rule: for each level, if the camera-to-tile distance² is LESS than the
// level's threshold, the child (higher-detail) representation is still
// required — so we return that (lower) level. When the distance exceeds all
// thresholds, we return `max_level` (the coarsest).
inline int SelectLodLevel(const Vec3& camera_pos, const Vec3& tile_origin,
                          float lod_scale, int max_level) {
    const float dx = camera_pos.x - tile_origin.x;
    const float dz = camera_pos.z - tile_origin.z;
    const float dist2 = dx * dx + dz * dz;
    const float base = kLevel2MinDistance * kLevel2MinDistance * lod_scale * lod_scale;
    for (int level = 0; level <= max_level; ++level) {
        // Higher levels have larger thresholds (parent replaces children when
        // the distance is large). Scale the base threshold by the level so
        // level 0 is the closest-switch.
        const float threshold = base * (1.0f + static_cast<float>(level));
        if (dist2 < threshold) return level;
    }
    return max_level;
}

}  // namespace madeline_cube
