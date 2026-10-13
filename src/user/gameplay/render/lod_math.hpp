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
        u.min.y = std::min(u.min.y, rooms[i].world_aabb.min.y);
        u.min.z = std::min(u.min.z, rooms[i].world_aabb.min.z);
        u.max.x = std::max(u.max.x, rooms[i].world_aabb.max.x);
        u.max.y = std::max(u.max.y, rooms[i].world_aabb.max.y);
        u.max.z = std::max(u.max.z, rooms[i].world_aabb.max.z);
    }
    return u;
}

// Perspective far plane covering the full map from any camera position inside
// its bounds. Include vertical extent: camera elevation contributes to view
// depth even though the conservative cell-cone test below is horizontal.
inline float MapFarClipDistance(const AABB* world_bounds,
                               float margin = 1.15f) {
    if (!world_bounds) return 0.0f;
    const float dx = world_bounds->max.x - world_bounds->min.x;
    const float dy = world_bounds->max.y - world_bounds->min.y;
    const float dz = world_bounds->max.z - world_bounds->min.z;
    if (dx <= 0.0f && dy <= 0.0f && dz <= 0.0f) return 0.0f;
    if (margin < 1.0f) margin = 1.0f;
    return std::sqrt(dx * dx + dy * dy + dz * dz) * margin;
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

// Conservative 3D frustum test for coarse map cells. Test the AABB support
// point against all six frustum planes. Unlike a bounding sphere, this avoids
// retaining large off-screen cells just because their enclosing sphere
// overlaps the view; plane support remains conservative (never rejects an
// AABB that intersects a plane-bounded frustum). Camera pitch and cell height
// are included, avoiding the old XZ-only false negatives.
inline bool CellAabbInFrustum3D(const Vec3& cam_pos, const Vec3& cam_target,
                                const Vec3& cam_up, float vertical_fov_deg,
                                float aspect_ratio, float near_d, float far_d,
                                const AABB& aabb, float margin = kCullMargin) {
    if (near_d >= far_d || far_d <= 0.0f || aspect_ratio <= 0.0f) return false;
    if (margin < 1.0f) margin = 1.0f;
    if (aabb.min.x > aabb.max.x || aabb.min.y > aabb.max.y ||
        aabb.min.z > aabb.max.z) return true;

    Vec3 forward = {cam_target.x - cam_pos.x,
                    cam_target.y - cam_pos.y,
                    cam_target.z - cam_pos.z};
    const float forward_len2 = forward.x * forward.x +
                               forward.y * forward.y +
                               forward.z * forward.z;
    if (forward_len2 < 1e-8f) return true;  // degenerate camera: don't pop
    const float inv_forward_len = 1.0f / std::sqrt(forward_len2);
    forward.x *= inv_forward_len;
    forward.y *= inv_forward_len;
    forward.z *= inv_forward_len;

    Vec3 right = {
        forward.y * cam_up.z - forward.z * cam_up.y,
        forward.z * cam_up.x - forward.x * cam_up.z,
        forward.x * cam_up.y - forward.y * cam_up.x,
    };
    const float right_len2 = right.x * right.x + right.y * right.y +
                             right.z * right.z;
    if (right_len2 < 1e-8f) return true;
    const float inv_right_len = 1.0f / std::sqrt(right_len2);
    right.x *= inv_right_len;
    right.y *= inv_right_len;
    right.z *= inv_right_len;
    const Vec3 view_up = {
        right.y * forward.z - right.z * forward.y,
        right.z * forward.x - right.x * forward.z,
        right.x * forward.y - right.y * forward.x,
    };

    const Vec3 center = {
        (aabb.min.x + aabb.max.x) * 0.5f,
        (aabb.min.y + aabb.max.y) * 0.5f,
        (aabb.min.z + aabb.max.z) * 0.5f,
    };
    const float hx = (aabb.max.x - aabb.min.x) * 0.5f;
    const float hy = (aabb.max.y - aabb.min.y) * 0.5f;
    const float hz = (aabb.max.z - aabb.min.z) * 0.5f;
    const Vec3 delta = {center.x - cam_pos.x,
                        center.y - cam_pos.y,
                        center.z - cam_pos.z};

    // A plane is kept if the AABB's positive support point is on or in front
    // of it. Plane normals need not be normalized: support and signed distance
    // use same scale. The tiny epsilon absorbs fixed-point camera roundoff.
    const auto outside_plane = [&](float nx, float ny, float nz, float offset) {
        const float signed_center = delta.x * nx + delta.y * ny +
                                    delta.z * nz + offset;
        const float support = std::fabs(nx) * hx + std::fabs(ny) * hy +
                              std::fabs(nz) * hz;
        return signed_center + support < -1e-4f;
    };
    if (outside_plane(forward.x, forward.y, forward.z, -near_d) ||
        outside_plane(-forward.x, -forward.y, -forward.z, far_d)) return false;

    float half_vertical = vertical_fov_deg * 0.5f * (kLodPi / 180.0f);
    if (half_vertical <= 0.0f || half_vertical >= 0.5f * kLodPi) return true;
    const float tan_v = std::tan(half_vertical) * margin;
    const float tan_h = tan_v * aspect_ratio;
    const Vec3 horizontal_left = {
        right.x + forward.x * tan_h,
        right.y + forward.y * tan_h,
        right.z + forward.z * tan_h,
    };
    const Vec3 horizontal_right = {
        forward.x * tan_h - right.x,
        forward.y * tan_h - right.y,
        forward.z * tan_h - right.z,
    };
    const Vec3 vertical_bottom = {
        view_up.x + forward.x * tan_v,
        view_up.y + forward.y * tan_v,
        view_up.z + forward.z * tan_v,
    };
    const Vec3 vertical_top = {
        forward.x * tan_v - view_up.x,
        forward.y * tan_v - view_up.y,
        forward.z * tan_v - view_up.z,
    };
    if (outside_plane(horizontal_left.x, horizontal_left.y,
                      horizontal_left.z, 0.0f) ||
        outside_plane(horizontal_right.x, horizontal_right.y,
                      horizontal_right.z, 0.0f) ||
        outside_plane(vertical_bottom.x, vertical_bottom.y,
                      vertical_bottom.z, 0.0f) ||
        outside_plane(vertical_top.x, vertical_top.y,
                      vertical_top.z, 0.0f)) return false;
    return true;
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
                               const AABB& aabb,
                               float margin = kCullMargin) {
    if (near_d >= far_d) return false;
    if (far_d <= 0.0f) return false;
    if (margin < 1.0f) margin = 1.0f;

    // Horizontal facing direction (camera -> target, XZ plane).
    const float fx = cam_target.x - cam_pos.x;
    const float fz = cam_target.z - cam_pos.z;
    const float flen2 = fx * fx + fz * fz;
    if (flen2 < 1e-6f) return true;  // degenerate facing — draw (safe)
    const float inv_flen = 1.0f / std::sqrt(flen2);
    const float fdx = fx * inv_flen;
    const float fdz = fz * inv_flen;

    // Horizontal full FOV from the vertical FOV + 4:3 aspect, then widened by
    // caller-provided margin so on-screen geometry near the edge doesn't pop.
    const float vfov_rad = fov_deg * 0.5f * (kLodPi / 180.0f);
    const float hfov_deg = 2.0f * std::atan(std::tan(vfov_rad) * (4.0f / 3.0f)) *
                           (180.0f / kLodPi);
    const float half_rad = (hfov_deg * 0.5f) * (kLodPi / 180.0f) * margin;

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
